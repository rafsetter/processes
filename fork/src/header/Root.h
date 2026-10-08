#pragma once

// The root process: reads commands from stdin and writes replies to stdout.
// When stdin is not a console (another program drives it), every reply ends with an empty line.
int RunRoot(bool killTree);
