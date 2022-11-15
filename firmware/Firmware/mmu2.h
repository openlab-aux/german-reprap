/// @file
#pragma once
#include "mmu2_protocol_logic.h"

struct E_Step;

namespace MMU2 {

static constexpr uint8_t MAX_RETRIES = 3U;

/// @@TODO hmmm, 12 bytes... may be we can reduce that
struct xyz_pos_t {
    float xyz[3];
    xyz_pos_t()=default;
};

// general MMU setup for MK3
enum : uint8_t {
    FILAMENT_UNKNOWN = 0xffU
};

struct Version {
    uint8_t major, minor, build;
};

/// Top-level interface between Logic and Marlin.
/// Intentionally named MMU2 to be (almost) a drop-in replacement for the previous implementation.
/// Most of the public methods share the original naming convention as well.
class MMU2 {
public:
    MMU2();
    
    /// Powers ON the MMU, then initializes the UART and protocol logic
    void Start();
    
    /// Stops the protocol logic, closes the UART, powers OFF the MMU
    void Stop();
    
    /// States of a printer with the MMU:
    /// - Active
    /// - Connecting
    /// - Stopped
    /// 
    /// When the printer's FW starts, the MMU2 mode is either Stopped or NotResponding (based on user's preference).
    /// When the MMU successfully establishes communication, the state changes to Active.
    enum class xState : uint_fast8_t {
        Active, ///< MMU has been detected, connected, communicates and is ready to be worked with.
        Connecting, ///< MMU is connected but it doesn't communicate (yet). The user wants the MMU, but it is not ready to be worked with.
        Stopped ///< The user doesn't want the printer to work with the MMU. The MMU itself is not powered and does not work at all.
    };
    
    inline xState State() const { return state; }
    
    // @@TODO temporary wrappers to make old gcc survive the code
    inline bool Enabled()const { return State() == xState::Active; }

    /// Different levels of resetting the MMU
    enum ResetForm : uint8_t {
        Software = 0, ///< sends a X0 command into the MMU, the MMU will watchdog-reset itself
        ResetPin = 1, ///< trigger the reset pin of the MMU
        CutThePower = 2 ///< power off and power on (that includes +5V and +24V power lines)
    };

    /// Saved print state on error.
    enum SavedState: uint8_t {
        None = 0, // No state saved. 
        ParkExtruder = 1, // The extruder was parked. 
        Cooldown = 2, // The extruder was allowed to cool.
        CooldownPending = 4,
    };

    /// Source of operation error
    enum ErrorSource: uint8_t {
        ErrorSourcePrinter = 0,
        ErrorSourceMMU = 1,
        ErrorSourceNone = 0xFF,
    };

    /// Perform a reset of the MMU
    /// @param level physical form of the reset
    void Reset(ResetForm level);
    
    /// Power off the MMU (cut the power)
    void PowerOff();
    
    /// Power on the MMU
    void PowerOn();

    /// Read from a MMU register (See gcode M707)
    /// @param address Address of register in hexidecimal
    /// @returns true upon success
    bool ReadRegister(uint8_t address);

    /// Write from a MMU register (See gcode M708)
    /// @param address Address of register in hexidecimal
    /// @param data Data to write to register
    /// @returns true upon success
    bool WriteRegister(uint8_t address, uint16_t data);


    /// The main loop of MMU processing.
    /// Doesn't loop (block) inside, performs just one step of logic state machines.
    /// Also, internally it prevents recursive entries.
    void mmu_loop();

    /// The main MMU command - select a different slot
    /// @param index of the slot to be selected
    /// @returns false if the operation cannot be performed (Stopped)
    bool tool_change(uint8_t index);
    
    /// Handling of special Tx, Tc, T? commands
    bool tool_change(char code, uint8_t slot);

    /// Unload of filament in collaboration with the MMU.
    /// That includes rotating the printer's extruder in order to release filament.
    /// @returns false if the operation cannot be performed (Stopped or cold extruder)
    bool unload();

    /// Load (insert) filament just into the MMU (not into printer's nozzle)
    /// @returns false if the operation cannot be performed (Stopped)
    bool load_filament(uint8_t index);
    
    /// Load (push) filament from the MMU into the printer's nozzle
    /// @returns false if the operation cannot be performed (Stopped or cold extruder)
    bool load_filament_to_nozzle(uint8_t index);

    /// Move MMU's selector aside and push the selected filament forward.
    /// Usable for improving filament's tip or pulling the remaining piece of filament out completely.
    bool eject_filament(uint8_t index, bool recover);

    /// Issue a Cut command into the MMU
    /// Requires unloaded filament from the printer (obviously)
    /// @returns false if the operation cannot be performed (Stopped)
    bool cut_filament(uint8_t index);

    /// Issue a planned request for statistics data from MMU
    void get_statistics();

    /// Issue a Try-Load command
    /// It behaves very similarly like a ToolChange, but it doesn't load the filament
    /// all the way down to the nozzle. The sole purpose of this operation
    /// is to check, that the filament will be ready for printing.
    bool load_to_extruder(uint8_t index);

    /// @returns the active filament slot index (0-4) or 0xff in case of no active tool
    uint8_t get_current_tool() const;

    /// @returns The filament slot index (0 to 4) that will be loaded next, 0xff in case of no active tool change 
    uint8_t get_tool_change_tool() const;

    bool set_filament_type(uint8_t index, uint8_t type);

    /// Issue a "button" click into the MMU - to be used from Error screens of the MMU
    /// to select one of the 3 possible options to resolve the issue
    void Button(uint8_t index);
    
    /// Issue an explicit "homing" command into the MMU
    void Home(uint8_t mode);

    /// @returns current state of FINDA (true=filament present, false=filament not present)
    inline bool FindaDetectsFilament()const { return logic.FindaPressed(); }

    inline uint16_t TotalFailStatistics()const { return logic.FailStatistics(); }

    /// @returns Current error code
    inline ErrorCode MMUCurrentErrorCode() const { return logic.Error(); }

    /// @returns Last error source
    inline ErrorSource MMULastErrorSource() const { return lastErrorSource; }

    /// @returns the version of the connected MMU FW.
    /// In the future we'll return the trully detected FW version
    Version GetMMUFWVersion()const {
        if( State() == xState::Active ){
            return { logic.MmuFwVersionMajor(), logic.MmuFwVersionMinor(), logic.MmuFwVersionRevision() };
        } else {
            return { 0, 0, 0}; 
        }
    }

    // Helper variable to monitor knob in MMU error screen in blocking functions e.g. manage_response
    bool is_mmu_error_monitor_active;

    /// Method to read-only mmu_print_saved
    inline bool MMU_PRINT_SAVED() const { return mmu_print_saved != SavedState::None; }

    /// Automagically "press" a Retry button if we have any retry attempts left
    /// @param ec ErrorCode enum value
    /// @returns true if auto-retry is ongoing, false when retry is unavailable or retry attempts are all used up
    bool RetryIfPossible(uint16_t ec);

    /// Decrement the retry attempts, if in a retry. 
    // Called by the MMU protocol when a sent button is acknowledged.
    void DecrementRetryAttempts();

    /// Updates toolchange counter in EEPROM
    /// ATmega2560 EEPROM has only 100'000 write/erase cycles
    /// so we can't call this function on every tool change.
    void update_tool_change_counter_eeprom();

    /// @return count for toolchange in current print
    inline uint16_t read_toolchange_counter() const { return toolchange_counter; };

    /// Set toolchange counter to zero
    inline void reset_toolchange_counter() { toolchange_counter = 0; };

private:
    // Increment the toolchange counter via SRAM to reserve EEPROM write cycles
    inline void increment_tool_change_counter() { ++toolchange_counter; };

    /// Reset the retryAttempts back to the default value
    void ResetRetryAttempts();
    /// Perform software self-reset of the MMU (sends an X0 command)
    void ResetX0();
    
    /// Trigger reset pin of the MMU
    void TriggerResetPin();
    
    /// Perform power cycle of the MMU (cold boot)
    /// Please note this is a blocking operation (sleeps for some time inside while doing the power cycle)
    void PowerCycle();
    
    /// Stop the communication, but keep the MMU powered on (for scenarios with incorrect FW version)
    void StopKeepPowered();

    /// Along with the mmu_loop method, this loops until a response from the MMU is received and acts upon.
    /// In case of an error, it parks the print head and turns off nozzle heating
    void manage_response(const bool move_axes, const bool turn_off_nozzle);

    /// The inner private implementation of mmu_loop()
    /// which is NOT (!!!) recursion-guarded. Use caution - but we do need it during waiting for hotend resume to keep comms alive!
    /// @param reportErrors true if Errors should raise MMU Error screen, false otherwise
    void mmu_loop_inner(bool reportErrors);

    /// Performs one step of the protocol logic state machine
    /// and reports progress and errors if needed to attached ExtUIs.
    /// Updates the global state of MMU (Active/Connecting/Stopped) at runtime, see @ref State
    /// @param reportErrors true if Errors should raise MMU Error screen, false otherwise
    StepStatus LogicStep(bool reportErrors);
    
    void filament_ramming();
    void execute_extruder_sequence(const E_Step *sequence, uint8_t steps);

    /// Reports an error into attached ExtUIs
    /// @param ec error code, see ErrorCode
    /// @param res reporter error source, is either Printer (0) or MMU (1)
    void ReportError(ErrorCode ec, ErrorSource res);

    /// Reports progress of operations into attached ExtUIs
    /// @param pc progress code, see ProgressCode
    void ReportProgress(ProgressCode pc);
    
    /// Responds to a change of MMU's progress
    /// - plans additional steps, e.g. starts the E-motor after fsensor trigger
    void OnMMUProgressMsg(ProgressCode pc);
    /// Progress code changed - act accordingly
    void OnMMUProgressMsgChanged(ProgressCode pc);
    /// Repeated calls when progress code remains the same
    void OnMMUProgressMsgSame(ProgressCode pc);
    
    /// Save print and park the print head
    void SaveAndPark(bool move_axes, bool turn_off_nozzle);

    /// Resume hotend temperature, if it was cooled. Safe to call if we aren't saved.
    void ResumeHotendTemp();

    /// Resume position, if the extruder was parked. Safe to all if state was not saved.
    void ResumeUnpark();

    /// Check for any button/user input coming from the printer's UI
    void CheckUserInput();

    /// @brief Check whether to trigger a FINDA runout. If triggered this function will call M600 AUTO
    /// if SpoolJoin is enabled, otherwise M600 is called without AUTO which will prompt the user
    /// for the next filament slot to use
    void CheckFINDARunout();

    /// Entry check of all external commands.
    /// It can wait until the MMU becomes ready.
    /// Optionally, it can also emit/display an error screen and the user can decide what to do next.
    /// @returns false if the MMU is not ready to perform the command (for whatever reason)
    bool WaitForMMUReady();

    ProtocolLogic logic; ///< implementation of the protocol logic layer
    uint8_t extruder; ///< currently active slot in the MMU ... somewhat... not sure where to get it from yet
    uint8_t tool_change_extruder; ///< only used for UI purposes

    xyz_pos_t resume_position;
    int16_t resume_hotend_temp;
    
    ProgressCode lastProgressCode = ProgressCode::OK;
    ErrorCode lastErrorCode = ErrorCode::MMU_NOT_RESPONDING;
    ErrorSource lastErrorSource = ErrorSource::ErrorSourceNone;
    Buttons lastButton = Buttons::NoButton;

    StepStatus logicStepLastStatus;
    
    enum xState state;

    uint8_t mmu_print_saved;
    bool loadFilamentStarted;
    bool unloadFilamentStarted;
    
    friend struct LoadingToNozzleRAII;
    /// true in case we are doing the LoadToNozzle operation - that means the filament shall be loaded all the way down to the nozzle
    /// unlike the mid-print ToolChange commands, which only load the first ~30mm and then the G-code takes over.
    bool loadingToNozzle;
    
    bool inAutoRetry;
    uint8_t retryAttempts;
    uint16_t toolchange_counter;
};

/// following Marlin's way of doing stuff - one and only instance of MMU implementation in the code base
/// + avoiding buggy singletons on the AVR platform
extern MMU2 mmu2;

} // namespace MMU2
