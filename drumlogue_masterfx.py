from hvcclogue import LogueSDKV2Generator

class Drumlogue_masterfx(LogueSDKV2Generator):
    FIXED_PARAMS = ()
    BUILTIN_PARAMS = ("sys_tempo",)
    UNIT_NUM_INPUT = 4
    UNIT_NUM_OUTPUT = 2
    MAX_SDRAM_SIZE = 33554432
    MAX_UNIT_SIZE = 33554432

    def unit_type():
        return "masterfx"
