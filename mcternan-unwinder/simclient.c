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
 * File Description: Unwinding client that runs on PC with a target memory
 *   image to allow debugging.  The target memory image must be present
 *   in two files, memlow.dat and memhigh.dat.  These can be created by
 *   loading a target image into a debugger such as ARMSD, and then dumping
 *   the memory with a command such as the following:
 *
 *       PU memlow.dat 0,0xfffff
 *       PU memhigh.dat 0x7ff60000,+0xffffff
 *
 *   The SP and PC values at which unwinding should start also need to
 *   recorded and copied to UnwindStart() in unwarminder.c to then allow
 *   unwinding to start with the saved data.
 *
 *   Conventionally the code will be in the low area of memory, with the
 *   stack data in the high area.  If this is not the case for the system
 *   being inspected, the address ranges may need to be changed to
 *   accommodate the memory map being emulated, in which case SimClientInit()
 *   will also need changing such that memLowOffset and memHighOffset are
 *   set to values that match the image dump.
 **************************************************************************/

/***************************************************************************
 * Includes
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system.h"
#include "simclient.h"

/***************************************************************************
 * Prototypes
 ***************************************************************************/

Boolean CliReport(void *data, Int32 address);
Boolean CliReadW(Int32 a, Int32 *v);
Boolean CliReadH(Int32 a, Int16 *v);
Boolean CliReadB(Int32 a, Int8  *v);
Boolean CliWriteW(Int32 a, Int32 v);
Boolean CliInvalidateW(Int32 a);

/***************************************************************************
 * Variables
 ***************************************************************************/

/* Variables for simulating the memory system of the target */
static FILE          *memLow, *memHigh;
static unsigned long  memLowOffset, memHighOffset;
static Boolean        memInit = FALSE;

/* Call back functions to be used by the unwinder */
const UnwindCallbacks cliCallbacks =
    {
        CliReport,
        CliReadW,
        CliReadH,
        CliReadB
#if defined(UNW_DEBUG)
       ,printf
#endif
    };

/***************************************************************************
 * Functions
 ***************************************************************************/

/***************************************************************************
 *
 * Function:     SimClientRead
 *
 * Parameters:   address  - The memory address to read.
 *               len      - The number of bytes to read.
 *               dest     - Pointer to the address to populate with read
 *                           value.
 *
 * Returns:      TRUE if the read succeeded, otherwise FALSE.
 *
 * Description:  Reads from the simulated memory by fseek()ing to the
 *                required place in the file that contains the memory image
 *                for the sought address, and then reads the required number
 *                of bytes.  This function performs no alignment checks or
 *                endian swapping, and assumes that the target ARM is
 *                little endian, and that the PC is x86 architecture.
 ***************************************************************************/
Boolean SimClientRead(Int32 address, Int8 len, void *dest)
{
    FILE * f;

    if(address >= memHighOffset)
    {
        address -= memHighOffset;
        f       = memHigh;
    }
    else
    {
        address -= memLowOffset;
        f       = memLow;
    }

    if(fseek(f, address, SEEK_SET))
    {
        perror("fseek() failed: ");
    }

    printf("Read 0x%lx %s\n", ftell(f), f == memHigh ? "H" : "L");

    memset(dest, 0, len);
    if(fread(dest, len, 1, f) != 1)
    {
        perror("fread() failed: ");
        return FALSE;
    }

    return TRUE;
}


/***************************************************************************
 *
 * Function:     SimClientInit
 *
 * Parameters:   none
 *
 * Returns:      nothing
 *
 * Description:  Initialises the emulated memory image if not already
 *                 initialised.  This involves opening the two input files
 *                 and setting the offset values as appropriate.
 ***************************************************************************/
void SimClientInit()
{
    if(memInit) return;

    memLow  = fopen("memlow.dat", "rb");
    if(!memLow)
    {
        perror("Failed to open memlow.dat: ");
        exit(EXIT_FAILURE);
    }
    memLowOffset = 0;   /* Value may need changing depending */

    memHigh = fopen("memhigh.dat", "rb");
    if(!memHigh)
    {
        perror("Failed to open memhigh.dat: ");
        exit(EXIT_FAILURE);
    }
    memHighOffset = 0x7ff60000;

    memInit = TRUE;
}

/***************************************************************************
 * Callback functions
 ***************************************************************************/

/***************************************************************************
 *
 * Function:     CliReport
 *
 * Parameters:   data    - Pointer to data passed to UnwindStart()
 *               address - The return address of a stack frame.
 *
 * Returns:      TRUE if unwinding should continue, otherwise FALSE to
 *                 indicate that unwinding should stop.
 *
 * Description:  This function is called from the unwinder each time a stack
 *                 frame has been unwound.  The LSB of address indicates if
 *                 the processor is in ARM mode (LSB clear) or Thumb (LSB
 *                 set).
 *
 ***************************************************************************/
Boolean CliReport(void *data, Int32 address)
{
    CliStack *s = (CliStack *)data;

    printf("\nCliReport: 0x%08x\n", address);

    s->address[s->frameCount] = address;
    s->frameCount++;

    if(s->frameCount >= (sizeof(s->address) / sizeof(s->address[0])))
    {
        return FALSE;
    }
    else
    {
        return TRUE;
    }
}

Boolean CliReadW(const Int32 a, Int32 *v)
{
    SimClientInit();
    return SimClientRead(a, 4, v);
}

Boolean CliReadH(const Int32 a, Int16 *v)
{
    SimClientInit();
    return SimClientRead(a, 2, v);
}

Boolean CliReadB(const Int32 a, Int8 *v)
{
    SimClientInit();
    return SimClientRead(a, 1, v);
}


int main()
{
    CliStack  results;
    Int8      t;
    UnwResult r;

    (results).frameCount = 0;
    /* SP value is filled by UnwindStart for the sim client */
    r = UnwindStart(0, &cliCallbacks, &results);

    for(t = 0; t < (results).frameCount; t++)
    {
        printf("%c: 0x%08x\n",
               (results.address[t] & 0x1) ? 'T' : 'A',
               results.address[t] & (~0x1));
    }

    printf("\nResult: %d\n", r);

    exit(EXIT_SUCCESS);
}


/* END OF FILE */
