"""Per-CDC binary protocol clients.

Each submodule wraps one of the firmware's host_proto layers:

==================  ====================================  =====
Module              Wire layer                            CDC
==================  ====================================  =====
emfi.py             F4 emfi_proto (CMD 0x01, 0x10..0x15)  CDC0
crowbar.py          F5 crowbar_proto (CMD 0x01, 0x10..0x14) CDC1
campaign.py         F9-4 campaign_proto (CMD 0x20..0x24)  CDC0/CDC1
                    multiplexed; engine implied by CDC
==================  ====================================  =====

Common framing primitives live in :mod:`faultycmd.framing`; the
shared client base in :mod:`faultycmd.protocols._base` opens the
serial port and round-trips frames.

The legacy reference clients in ``tools/{emfi,crowbar,campaign}_
client.py`` remain in place as a debugging fallback through v3.0.0
release; F11 archive retires them.
"""

from ._base import BinaryProtoClient, EngineError, ProtocolError
from .campaign import CampaignClient, CampaignResult, CampaignStatus
from .crowbar import CrowbarClient, CrowbarStatus
from .emfi import EmfiClient, EmfiStatus
from .scanner import ScannerClient, ScannerError

__all__ = [
    "BinaryProtoClient",
    "EngineError",
    "ProtocolError",
    "CampaignClient",
    "CampaignResult",
    "CampaignStatus",
    "CrowbarClient",
    "CrowbarStatus",
    "EmfiClient",
    "EmfiStatus",
    "ScannerClient",
    "ScannerError",
]
