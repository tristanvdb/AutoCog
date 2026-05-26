#ifndef AUTOCOG_RUNTIME_FRAME_STACK_HXX
#define AUTOCOG_RUNTIME_FRAME_STACK_HXX

// FrameStack: per-prompt frame history
//
// Maps prompt names to stacks of Frames. Used by:
//   - pop: resolve channels against the stack → initial content for a prompt
//   - push: parse LLM results back into a frame, push onto stack
//
// TODO: implement

namespace autocog::runtime::frame {

}

#endif // AUTOCOG_RUNTIME_FRAME_STACK_HXX
