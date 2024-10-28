#ifndef REQUESTS_HEADER
#define REQUESTS_HEADER
#include <stdbool.h>

#define MAX_IDX 48

// Enumeration for different request types
typedef enum {
    SCHEDULE_REQUEST = 1,
    TIME_STATUS_REQUEST,
    PLANE_STATUS_REQUEST
} request_type_t;

// Enumeration for different response types
typedef enum {
    SCHEDULE_RESPONSE = 1,
    TIME_STATUS_RESPONSE,
    PLANE_STATUS_RESPONSE
} response_type_t;

// Single structure for all requests
typedef struct {
    request_type_t type;  // Common field: type of request

    union {
        // Fields specific to SCHEDULE request
        struct {
            int plane_id;
            int earliest_time;
            int duration;
            int fuel;
        } schedule;

        // Fields specific to TIME_STATUS request
        struct {
            int gate_num;
            int start_idx;
            int duration;
        } time_status;

        // Fields specific to PLANE_STATUS request
        struct {
            int plane_id;
        } plane_status;
    } data; // Union for specific request data
} request_t;

// Single structure for all responses
typedef struct {
    response_type_t type;  // Common field: type of response
    int airport_id;        // airport_id of the airport responding to a request

    union {
        // Fields specific to SCHEDULE response
        struct {
            bool success;       // tells whether a plane was sucessfully allocated a spot in a gate
            int plane_id;       // id of the scheduled plane
            int gate_num;       // gate number at which we scheduled the plane
            int start_time;     // time rep as an int
            int end_time;

        } schedule;

        // Fields specific to TIME_STATUS response
        struct {
            // some stuff contained in arrays where ith element corresponds to the start_idx + i index values
            int num_idxs;
            int idxs[MAX_IDX];
            char statuses[MAX_IDX];
            int pl_ids[MAX_IDX];
        } time_status;

        // Fields specific to PLANE_STATUS response
        struct {
            int plane_id;
            bool scheduled; // was the above plane scheduled at a gate?
            int gate_num;
            int start_time; 
            int end_time;
        } plane_status;
    } data; // Union for specific request data
} response_t;

#endif