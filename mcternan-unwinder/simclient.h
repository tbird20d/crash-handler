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
 * File Description:  Unwinding client that runs on PC with a target memory
 *                      image to allow debugging.
 **************************************************************************/

#ifndef CLIENT_H
#define CLIENT_H

/***************************************************************************
 * Nested Includes
 ***************************************************************************/

#include <stdio.h>
#include "unwarminder.h"

/***************************************************************************
 * Types
 ***************************************************************************/

/** Example structure for holding unwind results.
 */
typedef struct
{
    /** Count of frames unwound. */
    Int16 frameCount;

    /** Storage for the return address from each stack frame.
     * Upto 32 frames will be unwound before unwinding is stopped.
     */
    Int32 address[32];
}
CliStack;


/***************************************************************************
 * Global Variables
 ***************************************************************************/

extern const UnwindCallbacks cliCallbacks;

#endif

/* END OF FILE */
