/*
 mission_planner.c
 Enhanced Space Mission Planner
 Author: Copilot (adapted for Ravi123k)
 Date: 2025-11-24

 Overview:
  - Refines the original mission planner by adding:
    * Clearer descriptions and comments
    * Input validation and a menu loop
    * Rocket & target listing with details
    * Delta-V budget breakdown printing
    * Simple refueling/tanker planning recommendations
    * Option to save mission results to a text file
    * Better date handling/formatting and help text
    * More descriptive timeline and travel-time estimates

  - Note: This is a lightweight planner for demonstration/educational use.
    It contains simplified astrodynamics approximations and assumptions.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Constants */
#define G0 9.80665
#define EARTH_ASCENT_COST 9.30 /* km/s - approximate delta-v to escape from LEO incl. losses */

/* Console colors (may not work on all terminals) */
#define CYAN "\033[1;36m"
#define GREEN "\033[1;32m"
#define RED "\033[1;31m"
#define YELLOW "\033[1;33m"
#define MAGENTA "\033[1;35m"
#define RESET "\033[0m"

#define MAX_LINE 256
#define DATE_STRLEN 20

/* Data structures */
typedef struct {
    char name[64];
    double wet_mass_kg;     /* total wet mass in kg */
    double dry_mass_kg;     /* dry mass in kg (no fuel) */
    double isp_avg;         /* average specific impulse (s) */
    double payload_leo_kg;  /* practical payload to LEO */
    double staging_factor;  /* empirical multiplier for multi-stage performance */
    double refuel_dv_per_tanker; /* extra delta-v (km/s) gained per tanker refuel mission (estimate) */
} Rocket;

typedef struct {
    char name[64];
    double dv_transfer;     /* km/s - transfer DV estimate */
    double dv_capture;      /* km/s - capture/braking DV estimate */
    double synodic_days;    /* days between favorable windows (synodic) */
    char epoch_date[DATE_STRLEN];    /* reference epoch for windows */
    double typical_transit_days; /* typical travel days (approx) */
} Body;

typedef struct {
    Rocket rocket;
    Body body;
    char start_date[DATE_STRLEN];
    double payload_kg;
    int strategy; /* 0=direct,1=oberth,2=gravity-assist,3=refuel,4=kick-stage,-1=impossible */
    char notes[256];
} Mission;

/* Predefined rockets and bodies (expanded metadata) */
Rocket rockets[] = {
    {"SpaceX's Starship", 5000000.0, 200000.0, 350.0, 150000.0, 1.4, 5.5},
    {"NASA's SLS", 2600000.0, 110000.0, 400.0, 95000.0, 1.5, 0.0},
    {"Blue Origin's New Glenn", 1700000.0, 100000.0, 340.0, 45000.0, 1.4, 0.0},
    {"ISRO's Mangalyaan 1 (PSLV)", 320000.0, 42000.0, 275.0, 1750.0, 1.2, 0.0}
};

Body bodies[] = {
    {"Moon", 3.12, 2.80, 29.5, "2025-01-13", 3.0},
    {"Mars", 3.80, 2.10, 780.0, "2025-01-16", 210.0},
    {"Titan (Saturn)", 7.30, 3.00, 378.1, "2025-09-21", 1000.0}
};

int NUM_ROCKETS = sizeof(rockets)/sizeof(rockets[0]);
int NUM_BODIES  = sizeof(bodies)/sizeof(bodies[0]);

/* Utility: flush stdin */
void clean_stdin() { int c; while((c = getchar()) != '\n' && c != EOF); }

/* Parse date string YYYY-MM-DD to epoch time (at 00:00:00 local). Returns -1 on error. */
time_t parse_date(const char* s) {
    struct tm tm = {0};
    int y,m,d;
    if(!s || strlen(s) < 8) return -1;
    if(sscanf(s,"%d-%d-%d",&y,&m,&d)!=3) return -1;
    tm.tm_year = y - 1900;
    tm.tm_mon  = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = 0;
    tm.tm_min = 0;
    tm.tm_sec = 0;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

/* Format time_t -> YYYY-MM-DD */
void format_date(time_t t, char* s) {
    if(t < 0) { strcpy(s, "----"); return; }
    struct tm* tm = localtime(&t);
    strftime(s, DATE_STRLEN, "%Y-%m-%d", tm);
}

/* Print a bright separator */
void print_separator() {
    printf(CYAN "+--------------------------------------------------------------------------------+\n" RESET);
}

/* Compute delta-v capability of a rocket for given payload (simple rocket equation macro) */
double calc_capability(const Rocket* r, double payload) {
    /* If payload exceeds practical LEO payload we consider it impossible for direct insertion */
    if(payload > r->payload_leo_kg) return 0.0;
    double m0 = r->wet_mass_kg + payload;
    double mf = r->dry_mass_kg + payload;
    if(mf <= 0 || m0 <= mf) return 0.0;
    double dv = (r->isp_avg * G0 * log(m0/mf)) / 1000.0; /* km/s */
    dv *= r->staging_factor; /* empirical boost for staging */
    return dv;
}

/* Print full rocket details */
void print_rocket_details(const Rocket* r, int idx) {
    printf(" %d) %s\n", idx+1, r->name);
    printf("    Wet mass:   %.0f kg | Dry mass: %.0f kg | Payload LEO: %.0f kg\n", r->wet_mass_kg, r->dry_mass_kg, r->payload_leo_kg);
    printf("    Isp_avg:    %.1f s   | Staging factor: %.2f | Tanker DV/mission: %.2f km/s\n",
           r->isp_avg, r->staging_factor, r->refuel_dv_per_tanker);
}

/* Print full body details */
void print_body_details(const Body* b, int idx) {
    printf(" %d) %s\n", idx+1, b->name);
    printf("    DV transfer: %.2f km/s | DV capture: %.2f km/s | Synodic: %.1f days\n",
           b->dv_transfer, b->dv_capture, b->synodic_days);
    printf("    Epoch: %s | Typical transit: %.0f days\n", b->epoch_date, b->typical_transit_days);
}

/* Display menu of rockets and bodies */
void list_available_options() {
    printf("\nAvailable Rockets:\n");
    for(int i=0;i<NUM_ROCKETS;i++) {
        print_rocket_details(&rockets[i], i);
    }
    printf("\nAvailable Destinations:\n");
    for(int i=0;i<NUM_BODIES;i++) {
        print_body_details(&bodies[i], i);
    }
    printf("\n");
}

/* Print mission summary header */
void print_mission_header(const Mission* m) {
    print_separator();
    printf(" MISSION SUMMARY\n");
    print_separator();
    printf(" Rocket:  %s\n", m->rocket.name);
    printf(" Target:  %s\n", m->body.name);
    printf(" Launch date (start): %s\n", m->start_date);
    printf(" Payload mass: %.0f kg\n", m->payload_kg);
    print_separator();
}

/* Print delta-v breakdown and margin */
void print_dv_breakdown(const Mission* m, double capability, double total_required, double final_cap, double final_margin) {
    printf("\n" MAGENTA " Delta-V Budget Breakdown (km/s):\n" RESET);
    printf("  - Earth ascent (LEO):      %.2f\n", EARTH_ASCENT_COST);
    printf("  - Transfer DV (to target): %.2f\n", m->body.dv_transfer);
    printf("  - Capture DV (arrival):    %.2f\n", m->body.dv_capture);
    printf("  ---------------------------------\n");
    printf("  - Total required:          %.2f km/s\n", total_required);
    printf("  - Rocket base capability:  %.2f km/s\n", capability);
    printf("  - Final mission capability:%.2f km/s\n", final_cap);
    if(final_margin >= 0) {
        printf(GREEN "  - Margin: +%.2f km/s [FEASIBLE]\n" RESET, final_margin);
    } else {
        printf(RED "  - Margin: %.2f km/s [INSUFFICIENT]\n" RESET, final_margin);
    }
}

/* Suggest refuel plan if applicable */
int compute_tanker_plan(const Mission* m, double shortage, int* tankers_needed) {
    /* Only rockets with non-zero refuel_dv_per_tanker realistically can use tankers (Starship) */
    double per_tanker = m->rocket.refuel_dv_per_tanker;
    if(per_tanker <= 0.0 || shortage <= 0.0) {
        *tankers_needed = 0;
        return 0;
    }
    int needed = (int)ceil(shortage / per_tanker);
    *tankers_needed = needed;
    return needed;
}

/* Print an enhanced timeline for the mission based on strategy */
void print_enhanced_timeline(const Mission* m) {
    printf("\n" CYAN " Mission Chronology & Notes:\n" RESET);
    print_separator();
    printf(" | %-24s | %-15s | %-34s |\n", "FLIGHT REGIME", "T-MINUS/PLUS", "ASTRODYNAMIC EVENT");
    print_separator();

    printf(" | %-24s | %-15s | %-34s |\n", "Pre-Launch", "T- 00:00:10", "Final Systems Checkout");
    printf(" | %-24s | %-15s | %-34s |\n", "Atmospheric Ascent", "T+ 00:01:00", "Max-Q / Stack Separation");
    printf(" | %-24s | %-15s | %-34s |\n", "LEO Insertion", "T+ 00:08:30", "Circularize / Prepare for Ops");

    if(m->strategy == 3) {
        printf(" | %-24s | %-15s | %-34s |\n", "Orbital Rendezvous", "T+ 12h - 48h", "Tanker Docking & Fuel Transfer");
        printf(" | %-24s | %-15s | %-34s |\n", "Departure Burn", "T+ 1-2d", "Full Injection to Interplanetary Trajectory");
    } else if(m->strategy == 4) {
        printf(" | %-24s | %-15s | %-34s |\n", "Kick Stage Ignition", "T+ 01:00:00", "Final Impulsive Injection");
    } else if(m->strategy == 2) {
        printf(" | %-24s | %-15s | %-34s |\n", "Gravity Assist Phase", "Years", "Multiple flybys (VEEGA/EGA approximation)");
    } else if(m->strategy == 1) {
        printf(" | %-24s | %-15s | %-34s |\n", "Oberth Kicks", "Days-Weeks", "Perigee burns to increase injection energy");
    } else {
        printf(" | %-24s | %-15s | %-34s |\n", "Trans Injection", "T+ 1-3d", "Escape / Trans-Target Burn");
    }

    printf(" | %-24s | %-15s | %-34s |\n", "Interplanetary Cruise", "Months-Years", "Mid-course Corrections & Trajectory Maintenance");
    printf(" | %-24s | %-15s | %-34s |\n", "Approach & Capture", "Arr - Days", "Terminal Descent & Insertion Ops");
    printf(" | %-24s | %-15s | %-34s |\n", "Landing/Arrival", "Arrival", "Surface contact / Orbit achieved");
    print_separator();

    if(strlen(m->notes) > 0) {
        printf("\n Notes: %s\n", m->notes);
    }
}

/* Save mission summary to a text file (human readable) */
int save_mission_to_file(const Mission* m, double capability, double total_required, double final_cap, double final_margin, int tankers) {
    char filename[128];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    snprintf(filename, sizeof(filename), "mission_%04d%02d%02d_%02d%02d.txt",
             tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min);

    FILE* f = fopen(filename, "w");
    if(!f) return -1;

    fprintf(f, "Mission planner output\n");
    fprintf(f, "Generated: %s\n\n", asctime(tm));
    fprintf(f, "Rocket: %s\n", m->rocket.name);
    fprintf(f, "Target: %s\n", m->body.name);
    fprintf(f, "Launch date: %s\n", m->start_date);
    fprintf(f, "Payload: %.0f kg\n\n", m->payload_kg);

    fprintf(f, "DV breakdown (km/s):\n");
    fprintf(f, "  Earth ascent: %.2f\n", EARTH_ASCENT_COST);
    fprintf(f, "  Transfer:     %.2f\n", m->body.dv_transfer);
    fprintf(f, "  Capture:      %.2f\n", m->body.dv_capture);
    fprintf(f, "  Total req:    %.2f\n", total_required);
    fprintf(f, "  Rocket base capability: %.2f\n", capability);
    fprintf(f, "  Final capability:       %.2f\n", final_cap);
    fprintf(f, "  Margin:                 %.2f\n\n", final_margin);

    if(tankers > 0) {
        fprintf(f, "Recommended tankers: %d\n", tankers);
    }
    if(strlen(m->notes) > 0) fprintf(f, "Notes: %s\n", m->notes);

    fclose(f);
    return 0;
}

/* Run mission planning and print results. Returns 0 on success. */
int run_mission(Mission* m) {
    if(!m) return -1;

    /* Base delta-v requirements */
    double p1 = EARTH_ASCENT_COST;
    double p2 = m->body.dv_transfer;
    double p3 = m->body.dv_capture;
    double total_req = p1 + p2 + p3;

    /* Compute rocket capability for payload */
    double cap = calc_capability(&m->rocket, m->payload_kg);

    /* Initialize strategy and notes */
    m->strategy = 0;
    strcpy(m->notes, "None");

    /* Decision logic: determine strategy if margin insufficient */
    double margin = cap - total_req;
    double bonus_dv = 0.0;

    if(margin < 0) {
        /* First consider gravity assist for long-reach bodies (Titan) */
        if(strstr(m->body.name, "Titan")) {
            m->strategy = 2;
            bonus_dv = 4.5; /* assumed gain via multi-flyby (VEEGA) */
            strcpy(m->notes, "Alternate route: VEEGA gravity assist (~7 year flight)");
        }
        /* If rocket can be refueled (e.g., Starship) suggest refueling */
        else if(strstr(m->rocket.name, "Starship")) {
            m->strategy = 3;
            /* In refuel strategy, we treat final capability as a fixed assumption or computed from tankers */
            strcpy(m->notes, "Assumption: LEO refueling by tanker missions");
            /* We'll compute tankers later */
        }
        /* If small negative margin, a kick stage may be assumed */
        else if(margin > -1.5) {
            m->strategy = 4;
            bonus_dv = 2.0;
            strcpy(m->notes, "Assumption: Added 'Star 48' solid kick stage");
        } else {
            /* Mark impossible initially */
            m->strategy = -1;
            strcpy(m->notes, "No feasible profile found with current assumptions");
        }
    } else {
        /* If rocket is small but the mission is Mars and payload light, might use Oberth/Perigee kicks */
        if(strstr(m->rocket.name, "PSLV") && strstr(m->body.name, "Mars") && m->payload_kg <= 1500) {
            m->strategy = 1;
            bonus_dv = 6.5;
            strcpy(m->notes, "Oberth/Kick-perigee method for low-mass Mars mission");
        }
    }

    /* If strategy is refuel, compute how many tankers required */
    int tankers_needed = 0;
    double final_cap = cap;
    double final_margin;

    if(m->strategy == 3) {
        /* If Starship, use its refuel_dv_per_tanker to estimate number of tankers.
           Compute positive shortage and plan tankers. */
        double shortage = total_req - cap;
        if(shortage <= 0) {
            tankers_needed = 0;
            final_cap = cap;
        } else {
            tankers_needed = compute_tanker_plan(m, shortage, &tankers_needed);
            if(tankers_needed < 0) tankers_needed = 0;
            final_cap = cap + (tankers_needed * m->rocket.refuel_dv_per_tanker);
            /* add note about tankers */
            char tmp[256];
            snprintf(tmp, sizeof(tmp), "LEO refueling: estimated %d tanker(s) required", tankers_needed);
            strncpy(m->notes, tmp, sizeof(m->notes)-1);
        }
    } else {
        final_cap = cap + bonus_dv;
    }

    final_margin = final_cap - total_req;
    int success = (final_margin >= 0 && m->strategy != -1);

    /* Print summary */
    printf("\n\n");
    print_mission_header(m);

    if(success) {
        if(m->strategy == 0) printf(GREEN " STATUS:   [ DIRECT MISSION FEASIBLE ]\n" RESET);
        else printf(YELLOW " STATUS:   [ ALTERNATE PROFILE FEASIBLE ]\n" RESET);
        if(strlen(m->notes)>0) printf(MAGENTA " METHOD:   %s\n" RESET, m->notes);
    } else {
        printf(RED " STATUS:   [ NOT FEASIBLE WITH CURRENT ASSUMPTIONS ]\n" RESET);
        printf(RED " RECOMMENDATION: Reduce payload or select a different launcher / strategy\n" RESET);
    }

    /* Tank usage illustrative */
    double fuel_pct = success ? 85.0 : 40.0;
    printf("\n TANK USAGE (approx): %.1f %%\n", fuel_pct);

    /* Print dv breakdown */
    print_dv_breakdown(m, cap, total_req, final_cap, final_margin);

    /* If refueling used, display tankers */
    if(m->strategy == 3 && tankers_needed > 0) {
        printf(YELLOW "\n Refueling Plan: Estimated tankers required: %d (each adds ~%.1f km/s)\n" RESET,
               tankers_needed, m->rocket.refuel_dv_per_tanker);
    }

    /* Provide suggestions: alternate rockets if impossible */
    if(!success) {
        printf("\n Suggestions:\n");
        for(int i=0;i<NUM_ROCKETS;i++) {
            if(strcmp(rockets[i].name, m->rocket.name)==0) continue;
            double alt_cap = calc_capability(&rockets[i], m->payload_kg);
            if(alt_cap - total_req >= 0) {
                printf("  - Use %s (cap %.2f km/s) could enable mission\n", rockets[i].name, alt_cap);
            }
        }
    }

    /* Print timeline and notes */
    if(success) print_enhanced_timeline(m);

    /* Print next five launch windows */
    time_t start = parse_date(m->start_date);
    time_t epoch = parse_date(m->body.epoch_date);
    double cycle_sec = m->body.synodic_days * 86400.0;
    double diff = difftime(start, epoch);
    int cycles = 0;
    if(diff <= 0) cycles = 0;
    else cycles = (int)floor(diff / cycle_sec);

    printf("\n" CYAN " NEXT 5 LAUNCH WINDOWS (estimated):\n" RESET);
    printf(" # | %-15s | %-15s\n", "LAUNCH DATE", "ARRIVAL (Est)");
    printf("----------------------------------------\n");
    for(int i=0;i<5;i++) {
        time_t launch = epoch + (time_t)((cycles + i) * cycle_sec);
        char l_str[DATE_STRLEN], a_str[DATE_STRLEN];
        format_date(launch, l_str);

        double days = m->body.typical_transit_days;
        /* For Titan gravity-assist strategy, adjust travel time */
        if(m->strategy == 2 && strstr(m->body.name,"Titan")) days = 2555.0; /* ~7 years */
        format_date(launch + (time_t)(days*86400.0), a_str);
        printf(" %d | %-15s | %-15s\n", i+1, l_str, a_str);
    }

    /* Option to save results */
    printf("\n Save mission summary to file? (y/N): ");
    clean_stdin();
    int c = getchar();
    if(c == 'y' || c=='Y') {
        int rc = save_mission_to_file(m, cap, total_req, final_cap, final_margin, tankers_needed);
        if(rc == 0) printf(GREEN " Saved mission summary to file.\n" RESET);
        else printf(RED " Failed to save mission summary to file.\n" RESET);
    }

    return 0;
}

/* Display help message */
void print_help() {
    printf("\nSpace Mission Planner Help\n");
    printf(" - This tool estimates whether a selected rocket can perform a mission to a chosen body\n");
    printf(" - It uses simplified delta-v budgets and empirical staging factors for capability\n");
    printf(" - Strategies considered: direct, Oberth/perigee kicks, gravity-assist, LEO refueling, kick-stage\n");
    printf(" - For serious mission design use dedicated astrodynamics tools and high-fidelity models\n\n");
}

/* Main interactive loop */
int main() {
    Mission m;
    memset(&m, 0, sizeof(m));

    printf("\n--- SPACE MISSION PLANNER (ENHANCED) ---\n");
    print_help();

    while(1) {
        printf("\nMain Menu:\n");
        printf(" 1) List available rockets & targets\n");
        printf(" 2) Plan a new mission\n");
        printf(" 3) Quit\n");
        printf("Selection > ");
        int choice = 0;
        if(scanf("%d", &choice) != 1) { clean_stdin(); choice = 0; }

        if(choice == 1) {
            list_available_options();
            continue;
        } else if(choice == 2) {
            /* Rocket selection */
            printf("\nSelect Rocket:\n");
            for(int i=0;i<NUM_ROCKETS;i++) {
                printf(" %d) %s\n", i+1, rockets[i].name);
            }
            printf("Selection > ");
            int rsel = 0;
            if(scanf("%d", &rsel) != 1) rsel = 1;
            if(rsel < 1 || rsel > NUM_ROCKETS) rsel = 1;
            m.rocket = rockets[rsel-1];

            /* Body selection */
            printf("\nSelect Destination:\n");
            for(int i=0;i<NUM_BODIES;i++) {
                printf(" %d) %s\n", i+1, bodies[i].name);
            }
            printf("Selection > ");
            int bsel = 0;
            if(scanf("%d", &bsel) != 1) bsel = 1;
            if(bsel < 1 || bsel > NUM_BODIES) bsel = 1;
            m.body = bodies[bsel-1];

            /* Start date */
            clean_stdin();
            printf("\nStart Date (YYYY-MM-DD) [default: 2025-01-01]: ");
            char buf[DATE_STRLEN] = {0};
            if(fgets(buf, DATE_STRLEN, stdin) == NULL) strcpy(buf, "2025-01-01");
            buf[strcspn(buf, "\n")] = 0;
            if(strlen(buf) < 8) strcpy(m.start_date, "2025-01-01");
            else strncpy(m.start_date, buf, DATE_STRLEN-1);

            /* Payload */
            printf("Payload Mass (kg) [enter numeric value]: ");
            double payload = 0.0;
            if(scanf("%lf", &payload) != 1) payload = 0.0;
            if(payload < 0) payload = 0.0;
            m.payload_kg = payload;

            /* Run the mission planner */
            run_mission(&m);

            /* After run, loop back */
            continue;
        } else if(choice == 3) {
            printf("\nExiting. Safe travels!\n");
            break;
        } else {
            printf("\nInvalid selection. Try again.\n");
        }
    }

    return 0;
}
