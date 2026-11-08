/**************************************************************************
 *									  *
 *		 Copyright (C) 1994, Silicon Graphics, Inc.		  *
 *									  *
 *  These coded instructions, statements, and computer programs  contain  *
 *  unpublished  proprietary  information of Silicon Graphics, Inc., and  *
 *  are protected by Federal copyright law.  They  may  not be disclosed  *
 *  to  third  parties  or copied or duplicated in any form, in whole or  *
 *  in part, without the prior written consent of Silicon Graphics, Inc.  *
 *									  *
 **************************************************************************/

#include <PR/guint.h>

void guMtxF2L(float mf[4][4], Mtx* m) {
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            int intValue = FTOFIX32(mf[i][j]);

            m->u.i[i][j] = intValue >> 16;
            m->u.f[i][j] = intValue;
        }
    }
}

void guMtxL2F(float mf[4][4], Mtx* m) {
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            int fixed = ((int) (short) m->u.i[i][j] << 16) | (unsigned int) m->u.f[i][j];

            mf[i][j] = (float) fixed / 65536.0f;
        }
    }
}

void guMtxIdentF(float mf[4][4]) {
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            if (i == j) {
                mf[i][j] = 1.0;
            } else {
                mf[i][j] = 0.0;
            }
        }
    }
}

void guMtxIdent(Mtx* m) {
    float mf[4][4];

    guMtxIdentF(mf);

    guMtxF2L(mf, m);
}
