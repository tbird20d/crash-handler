/***************************************************************************
 * ARM Stack Unwinder, Michael.McTernan.2001@cs.bris.ac.uk
 *
 * This program is PUBLIC DOMAIN.
 * This means that there is no copyright and anyone is able to take a copy
 * for free and use it as they wish, with or without modifications, and in
 * any context, commercially or otherwise. The only limitation is that I
 * don't guarantee that the software is fit for any purpose or accept any
 * liability for it's use or misuse - this software is without warranty.
 ***************************************************************************
 * File Description:  Specially constructed functions to test the unwinder.
 *   This file contains a mishmash of functions that compile in interesting
 *   ways at various levels of optimisation.  The functions call each
 *   other to nest in order to produce a stack that can then be unwound.
 **************************************************************************/

#include <stdio.h>
#include "client.h"

void testShellSort(void *,int a, int b);
void tailCall(int v);

void viaFuncPointer(void)
{
    printf("Func pointer func");
    tailCall(5);
}


typedef void (*fPoint)(void);

static fPoint runFunc = viaFuncPointer;



void wind()
{
  UNWIND();
}

void tailFunc(int v)
{
    int t[10], u;

    for(u = 0; u < 10; u++)
    {
        t[u] = u;
    }

    printf("%d %d", v, t[9]);

    t[u] += v;
}

void tailCall(int v)
{
    v *= v;

    wind();

    printf("%d", v);
    tailFunc(v);
}


int testStackResize(void)
{
    char biggie[0x81111];
    char *c = biggie;
    int  t;

    sprintf(biggie, "Hello");

    t = 0;

    while(*c)
    {
        t += *c;
        c++;
    }

    runFunc();
    return t;
}


int testConst()
{
    const int t = 5;
    int vals[5] = { 1, 2, 3, 4, 5 };

    printf("vals = %x\n %d", *vals, t);


    return testStackResize();
}

int testRecurse(int t)
{
    if(t > 0)
        return t + testRecurse(t - 1);
    else
    {
        return testConst();
    }
}

#pragma push
#pragma thumb
void testThumb1(int v)
{
    v += 1;
    v *=  2;

    printf("v1 = %d\n",v);

    testRecurse(4);
}


#pragma arm
void testArm1(int v)
{
    v += 4;
    v /=  2;

    printf("v = %d\n",v);

    testThumb1(v);
}


#pragma thumb
void testThumb(int v)
{
    v += 15;
    v *=  2;

    printf("v = %d\n",v);

    testArm1(v);
}


#pragma arm

void testArm(int v)
{
    v += 1;
    v *=  2;

    printf("v = %d\n",v);

    testThumb(v);
}

#pragma pop

int testPrintf(int t)
{
    printf("hello world %d\n", t);
    testArm(1);

    return t + 1;
}


void testVoid(void)
{
    testPrintf(5);
}

main()
{
    testVoid();
}
