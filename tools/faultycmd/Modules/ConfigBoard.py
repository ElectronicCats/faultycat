from enum import Enum

class Commands(Enum):
    COMMAND_HELP              = "h"
    COMMAND_ARM               = "a"
    COMMAND_DISARM            = "d"
    COMMAND_PULSE             = "p"
    COMMAND_ENABLE_TIMEOUT    = "en"
    COMMAND_DISABLE_TIMEOUT   = "di"
    COMMAND_FAST_TRIGGER      = "f"
    COMMAND_FAST_TRIGGER_CONF = "fa"
    COMMAND_INTERNAL_HVP      = "ih"
    COMMAND_EXTERNAL_HVP      = "eh"
    COMMAND_CONFIGURE         = "c"
    COMMAND_TOGGLE_GPIO       = "t"
    COMMAND_STATUS            = "s"
    COMMAND_RESET             = "r"
    
    def __str__(self):
        return self.value

class BoardStatus(Enum):
    STATUS_ARMED          = "armed"
    STATUS_DISARMED       = "disarmed"
    STATUS_CHARGED        = "charged"
    STATUS_PULSE          = "pulsed"
    STATUS_NOT_CHARGED    = "Not Charged"
    STATUS_TIMEOUT_ACTIVE = "Timeout active"
    STATUS_TIMEOUT_DEACT  = "Timeout deactivated"
    STATUS_HVP_INTERVAL   = "HVP interval"
    
    def __str__(self):
        return self.value
    
    @classmethod
    def get_status_by_value(cls, value):
        for status in cls.__members__.values():
            if status.value == value:
                return status
        return None

class ConfigBoard:
    BOARD_CONFIG = {
        "pulse_time" : 1.0,
        "pulse_power": 0.012200,
        "pulse_count": 1,
        "port"       : "COM1"
    }
    def __init__(self) -> None:
        self.board_config = ConfigBoard.BOARD_CONFIG
        self.board_commands = Commands
    
    def get_config(self) -> dict:
        return self.board_config

    def set_config(self, config: dict) -> None:
        self.board_config = config
    
    def __str__(self) -> str:
        return f"Board config: {self.board_config}"
    