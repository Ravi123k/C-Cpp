#include <stdio.h>
#include <string.h>
#include <math.h>

// ----- CONSTANTS, STRUCTURES, DATA -----

#define NUM_ROCKETS 4
#define NUM_BODIES 3
#define MAX_WINDOWS 5
#define NAME_MAX 40

typedef struct {
    char name[NAME_MAX];
    double isp;           // specific impulse (s)
    double dry_mass;      // kg
    double wet_mass;      // kg
    double max_dv;        // km/s, approximate max capability
} Rocket;

typedef struct {
    char launch_date[16];
    char arrival_date[16];
    double required_dv;   // delta-V needed for this window (km/s)
} LaunchWindow;

typedef struct {
    char name[NAME_MAX];
    double average_distance; // km
    double synodic_period;   // days
    double min_dv;           // km/s needed for minimum-energy transfer
    LaunchWindow windows[MAX_WINDOWS];
} CelestialBody;

// ----- HARD-CODED ROCKET & CELESTIAL BODY DATA -----

Rocket rockets[NUM_ROCKETS] = {
    {"SpaceX Starship",      380, 120000, 500000,   9.1},
    {"NASA SLS",             452,  85000,2600000,  10.0},
    {"Blue Origin New Glenn",350,  45000, 450000,   9.0},
    {"ISRO Mangalyaan 1",    315,   1350,   2200,   9.7}
};

CelestialBody bodies[NUM_BODIES] = {
    {
        "Moon", 384400, 29.53, 10.8,
        {
            {"2025-12-01","2025-12-04",10.8},
            {"2026-01-01","2026-01-04",10.8},
            {"2026-01-31","2026-02-03",10.8},
            {"2026-03-01","2026-03-04",10.8},
            {"2026-03-30","2026-04-02",10.8}
        }
    },{
        "Mars", 225000000, 780.0, 12.0,
        {
            {"2027-02-01","2027-09-01",12.0},
            {"2029-04-15","2029-11-20",12.0},
            {"2031-06-10","2032-01-10",12.0},
            {"2033-08-17","2034-03-12",12.0},
            {"2035-10-22","2036-05-30",12.0}
        }
    },{
        "Titan",1200000000,378.0,18.0,
        {
            {"2030-05-18","2037-01-15",18.0},
            {"2035-06-22","2042-02-01",18.0},
            {"2040-07-30","2047-03-10",18.0},
            {"2045-09-06","2052-04-18",18.0},
            {"2050-10-14","2057-05-27",18.0}
        }
    }
};

// ----- FUNCTIONS -----

int select_by_menu(const char *prompt, char names[][NAME_MAX], int n) {
    printf("%s\n", prompt);
    for (int i = 0; i < n; i++)
        printf("  %d. %s\n", i+1, names[i]);
    printf("Enter option number: ");
    int x; scanf("%d", &x);
    return (x >= 1 && x <= n) ? (x - 1) : -1;
}

double estimate_fuel(double dv_kms, Rocket *r) {
    double g0 = 9.81;
    double dv = dv_kms * 1000.0;
    double exponent = dv / (g0 * r->isp);
    double mass_ratio = exp(exponent);
    double fuel = r->dry_mass * (mass_ratio - 1);
    if (fuel < 0) fuel = 0;
    return fuel;
}

void print_mission_summary(Rocket *r, CelestialBody *b) {
    printf("\nMISSION SUMMARY\n");
    printf("Rocket: %s\n", r->name);
    printf("Dry Mass: %.0f kg   Wet Mass: %.0f kg\n", r->dry_mass, r->wet_mass);
    printf("Specific Impulse: %.0f s\n", r->isp);
    printf("Max Delta-V: %.2f km/s\n", r->max_dv);
    printf("Destination: %s\n", b->name);
    printf("Average Distance: %.0f km\n", b->average_distance);
    printf("Minimum Required Delta-V: %.2f km/s\n", b->min_dv);
    printf("--------------------------------------------\n");
}

void print_launch_windows(Rocket *r, CelestialBody *b) {
    printf("\nNext 5 Launch Windows for %s to %s:\n", r->name, b->name);
    printf("Launch Date    | Arrival Date   | Req Delta-V | Usable for Rocket   | Estimated Fuel (kg)\n");
    printf("---------------|----------------|-------------|---------------------|--------------------\n");
    for (int i = 0; i < MAX_WINDOWS; i++) {
        double req_dv = b->windows[i].required_dv;
        double dv_over = req_dv - r->max_dv;
        char status[40];
        if (dv_over <= 0.0) {
            strcpy(status, "YES");
        } else {
            snprintf(status, sizeof(status), "EXCEEDS BY %.1f", dv_over);
        }
        double fuel = estimate_fuel(req_dv, r);
        printf("%-14s | %-14s | %7.2f     | %-19s | %10.0f\n", 
               b->windows[i].launch_date,
               b->windows[i].arrival_date,
               req_dv,
               status,
               fuel);
    }
    printf("--------------------------------------------\n");
}

// ----- MAIN PROGRAM -----

int main() {
    char rocket_names[NUM_ROCKETS][NAME_MAX], body_names[NUM_BODIES][NAME_MAX];
    for (int i = 0; i < NUM_ROCKETS; i++) strcpy(rocket_names[i], rockets[i].name);
    for (int i = 0; i < NUM_BODIES; i++) strcpy(body_names[i], bodies[i].name);

    printf("----- SPACE MISSION PLANNER (Assignment Final) -----\n\n");

    int r_idx = select_by_menu("Select Rocket:", rocket_names, NUM_ROCKETS);
    if (r_idx == -1) { printf("Invalid rocket choice!\n"); return 1; }
    int b_idx = select_by_menu("Select Celestial Body:", body_names, NUM_BODIES);
    if (b_idx == -1) { printf("Invalid celestial body choice!\n"); return 1; }

    Rocket *r = &rockets[r_idx];
    CelestialBody *b = &bodies[b_idx];

    print_mission_summary(r, b);
    print_launch_windows(r, b);



    return 0;
}