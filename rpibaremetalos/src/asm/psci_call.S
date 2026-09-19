//  int64_t PSCICall(uint64_t function_id, uint64_t arg0, uint64_t arg1, uint64_t arg2)
//
//  SMCCC passes arguments and returns results in x0-x3, so the AAPCS64 argument registers
//      already sit where the call needs them and the result is already in x0.

.globl PSCICall
PSCICall:
	smc     #0
	ret
    