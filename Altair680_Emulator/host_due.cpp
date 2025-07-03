/*
 ---- front panel connections by function:

  For pins that are not labeled on the board with their digital number
  the board label is given in []

  Function switches:
     RUN          => D20 (PIOB12)
     HALT         => D21 (PIOB13)
     DEPOSIT      => D58 [A4] (PIOA6)
     RESET        => D52 (PIOB21)

   Address switches:
     SWA0...7      => D62 [A8], D63 [A9], D64 [A10], D65 [A11], D66 [DAC0], D67 [DAC1], D68 [CANRX], D69 [CANTX]
     SWA8...15     => D17,D16,D23,D24,D70[SDA1],D71[SCL1],D42,D43  (PIOA, bits 12-15,17-20)
     SWD0...7      => D2, D3, D4, D5, D6, D7, D8, D9

   Bus LEDs:
     A0..7        => 34, 35, ..., 41          (PIOC, bits 2-9)
     A8..15       => 51, 50, ..., 44          (PIOC, bits 12-19)
     D0..8        => 25,26,27,28,14,15,29,11  (PIOD, bits 0-7)

   Status LEDs:
     AC           => D12 (PIOD8)
     HALT         => D13 (PIOB27)
     RUN          => D10 (PIOC29)

*/

#define min(x, y) ((x)<(y) ? (x) : (y))
#define max(x, y) ((x)>(y) ? (x) : (y))

#define GETBIT(reg, regbit, v) (REG_PIO ## reg ## _PDSR & (1<<(regbit)) ? v : 0)
#define SETBIT(v, vbit, reg, regbit) if( v & vbit ) REG_PIO ## reg ## _SODR = 1<<regbit; else REG_PIO ## reg ## _CODR = 1<<regbit
