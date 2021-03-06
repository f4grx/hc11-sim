#!/bin/sh

#flag values
# C = 0x01
# V = 0x02
# Z = 0x04
# N = 0x08
SIM="./sim -g -w --run"

#test rotations in A and B
echo ROLA
#ROLA,C=0, each bit. Carry set is MSB of reg is rolled left, Neg set if 0x40 rolls to 0x80, Cy set of 0x80 rolls to 0.
#V set if N!=C
${SIM} -pa=0x00,c=0,p=0xE000 -m0xE000,49 -ea=0x00,c=0x04 #Z=0x04
${SIM} -pa=0x01,c=0,p=0xE000 -m0xE000,49 -ea=0x02,c=0x00
${SIM} -pa=0x02,c=0,p=0xE000 -m0xE000,49 -ea=0x04,c=0x00
${SIM} -pa=0x04,c=0,p=0xE000 -m0xE000,49 -ea=0x08,c=0x00
${SIM} -pa=0x08,c=0,p=0xE000 -m0xE000,49 -ea=0x10,c=0x00
${SIM} -pa=0x10,c=0,p=0xE000 -m0xE000,49 -ea=0x20,c=0x00
${SIM} -pa=0x20,c=0,p=0xE000 -m0xE000,49 -ea=0x40,c=0x00
${SIM} -pa=0x40,c=0,p=0xE000 -m0xE000,49 -ea=0x80,c=0x0A #N
${SIM} -pa=0x80,c=0,p=0xE000 -m0xE000,49 -ea=0x00,c=0x07 #C,Z

echo ROLB
#ROLA,C=0, each bit. Carry set is MSB of reg is rolled left, Neg set if 0x40 rolls to 0x80, Cy set of 0x80 rolls to 0.
#V set if N!=C
${SIM} -pb=0x00,c=0,p=0xE000 -m0xE000,59 -eb=0x00,c=0x04 #Z=0x04
${SIM} -pb=0x01,c=0,p=0xE000 -m0xE000,59 -eb=0x02,c=0x00
${SIM} -pb=0x02,c=0,p=0xE000 -m0xE000,59 -eb=0x04,c=0x00
${SIM} -pb=0x04,c=0,p=0xE000 -m0xE000,59 -eb=0x08,c=0x00
${SIM} -pb=0x08,c=0,p=0xE000 -m0xE000,59 -eb=0x10,c=0x00
${SIM} -pb=0x10,c=0,p=0xE000 -m0xE000,59 -eb=0x20,c=0x00
${SIM} -pb=0x20,c=0,p=0xE000 -m0xE000,59 -eb=0x40,c=0x00
${SIM} -pb=0x40,c=0,p=0xE000 -m0xE000,59 -eb=0x80,c=0x0A #N
${SIM} -pb=0x80,c=0,p=0xE000 -m0xE000,59 -eb=0x00,c=0x07 #C,Z


