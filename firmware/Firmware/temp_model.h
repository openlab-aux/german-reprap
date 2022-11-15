// model-based temperature safety checker declarations
#ifndef TEMP_MGR_INTV
#error "this file is not a public interface, it should be used *only* within temperature.cpp!"
#endif

#include "planner.h"

constexpr uint8_t TEMP_MODEL_CAL_S = 60;     // Maximum recording length during calibration (s)
constexpr uint8_t TEMP_MODEL_CAL_R_STEP = 4; // Fan interpolation steps during calibration
constexpr float TEMP_MODEL_fS = 0.065;       // simulation filter (1st-order IIR factor)
constexpr float TEMP_MODEL_fE = 0.05;        // error filter (1st-order IIR factor)

// transport delay buffer size (samples)
constexpr uint8_t TEMP_MODEL_LAG_SIZE = (TEMP_MODEL_LAG / TEMP_MGR_INTV + 0.5);

// resistance values for all fan levels
constexpr uint8_t TEMP_MODEL_R_SIZE = (1 << FAN_SOFT_PWM_BITS);

namespace temp_model {

struct model_data
{
    // temporary buffers
    float dT_lag_buf[TEMP_MODEL_LAG_SIZE]; // transport delay buffer
    uint8_t dT_lag_idx = 0;                // transport delay buffer index
    float dT_err_prev = 0;                 // previous temperature delta error
    float T_prev = 0;                      // last temperature extruder

    // configurable parameters
    float P;                               // heater power (W)
    float C;                               // heatblock capacitance (J/K)
    float R[TEMP_MODEL_R_SIZE];            // heatblock resistance for all fan levels (K/W)
    float Ta_corr;                         // ambient temperature correction (K)

    // thresholds
    float warn;                            // warning threshold (K/s)
    float err;                             // error threshold (K/s)

    // status flags
    union
    {
        bool flags;
        struct
        {
            bool uninitialized: 1;         // model is not initialized
            bool error: 1;                 // error threshold set
            bool warning: 1;               // warning threshold set
        } flag_bits;
    };

    // pre-computed values (initialized via reset)
    float C_i;                             // heatblock capacitance (precomputed dT/C)
    float warn_s;                          // warning threshold (per sample)
    float err_s;                           // error threshold (per sample)

    // simulation functions
    void reset(uint8_t heater_pwm, uint8_t fan_pwm, float heater_temp, float ambient_temp);
    void step(uint8_t heater_pwm, uint8_t fan_pwm, float heater_temp, float ambient_temp);
};

static bool enabled;          // model check enabled
static bool valid = false;    // model is valid
static bool warn_beep = true; // beep on warning threshold
static model_data data;       // default heater data

static bool calibrated(); // return calibration/model validity status
static void check();      // check and trigger errors or warnings based on current state

// warning state (updated from from isr context)
volatile static struct
{
    float dT_err;    // temperature delta error (per sample)
    bool warning: 1; // warning condition
    bool assert: 1;  // warning is still asserted
} warning_state;

static void handle_warning(); // handle warnings from user context

#ifdef TEMP_MODEL_DEBUG
static struct
{
    volatile struct
    {
        uint32_t stamp;
        int8_t delta_ms;
        uint8_t counter;
        uint8_t cur_pwm;
        float cur_temp;
        float cur_amb;
    } entry;

    uint8_t serial;
    bool enabled;
} log_buf;

static void log_usr(); // user log handler
static void log_isr(); // isr log handler
#endif

} // namespace temp_model

namespace temp_model_cal {

// recording scratch buffer
struct rec_entry
{
    float temp;  // heater temperature
    uint8_t pwm; // heater PWM
};

constexpr uint16_t REC_BUFFER_SIZE = TEMP_MODEL_CAL_S / TEMP_MGR_INTV;
static rec_entry* const rec_buffer = (rec_entry*)block_buffer; // oh-hey, free memory!
static_assert(sizeof(rec_entry[REC_BUFFER_SIZE]) <= sizeof(block_buffer),
    "recording length too long to fit within available buffer");

} // namespace temp_model_cal
