Protection Configuration Register (Settings:Protection)
This register contains the master PF_FUSE bit. If this is 0, a permanent failure will turn off the FETs but will not blow the fuse.

Bit	Field	Description
10	SCDL_CURR_RECOV	SCDL recovers when charge current is detected
9	OCDL_CURR_RECOV	OCDL recovers when charge current is detected
8	FETF_FUSE	1 = Blow fuse on FET failure regardless of voltage
7	PACK_FUSE	1 = Use PACK voltage instead of Top of Stack for fuse blow check
5	PF_OTP	1 = Preserve PF status in OTP across resets
4	PF_FUSE	1 = Permanent Failure causes the device to blow the fuse
3	PF_DPSLP	1 = Enter DEEPSLEEP on PF
2	PF_REGS	1 = Turn regulators off on PF
1	PF_FETS	
1 = Turn FETs off on PF 
BQ76952_tech_manual.pdf

Enabled PF A Register (Settings:Permanent Failure)
Bit	Field	Description
7	CUDEP	Copper Deposition
6	SOTF	Safety Overtemperature FET
4	SOT	Safety Overtemperature
3	SOCD	Safety Overcurrent in Discharge
2	SOCC	Safety Overcurrent in Charge
1	SOV	Safety Cell Overvoltage
0	SUV	
Safety Cell Undervoltage 
BQ76952_tech_manual.pdf

Enabled PF B Register (Settings:Permanent Failure)
Bit	Field	Description
7	SCDL	Short Circuit in Discharge Latch
4	VIMA	Voltage Imbalance Active
3	VIMR	Voltage Imbalance at Rest
2	2LVL	Second Level Protector
1	DFETF	Discharge FET Failure
0	CFETF	
Charge FET Failure 
BQ76952_tech_manual.pdf

Enabled PF C Register (Settings:Permanent Failure)
Bit	Field	Description
7	CMDF	Commanded Permanent Fail
6	HWMX	Internal Stuck Hardware Mux
5	VSSF	Internal VSS Measurement
4	VREF	Internal Voltage Reference
3	LFOF	Internal LFO
2	IRMF	Instruction ROM
1	DRMF	Data ROM
0	OTPF	
OTP Memory 
BQ76952_tech_manual.pdf