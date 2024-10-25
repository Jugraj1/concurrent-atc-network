#ifndef REQUESTS_HEADER
#define REQUESTS_HEADER

// Enumeration for different request types
typedef enum {
    SCHEDULE_REQUEST = 1,
    TIME_STATUS_REQUEST,
    PLANE_STATUS_REQUEST
} request_type_t;

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

#endif