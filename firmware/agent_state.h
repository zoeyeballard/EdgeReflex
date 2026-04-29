//*****************************************************************************
// agent_state.h - Minimal persistent state for agentic inference loops.
//*****************************************************************************

#ifndef __AGENT_STATE_H__
#define __AGENT_STATE_H__

#include <stdint.h>

#define NUM_CLASSES  4U

typedef struct
{
    int8_t   prev_probs[NUM_CLASSES];
    uint8_t  confidence;
    uint32_t step;
    uint32_t last_update_ms;
} AgentState_t;

#endif // __AGENT_STATE_H__