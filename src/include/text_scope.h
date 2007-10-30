#ifndef _TEXT_SCOPE_H_
#define _TEXT_SCOPE_H_

enum PaulaEventType {PET_VOL, PET_PER, PET_DAT, PET_LEN, PET_LCH, PET_LCL};

void text_scope(unsigned long cycles, int voice, enum PaulaEventType e,
		int value);

#endif
