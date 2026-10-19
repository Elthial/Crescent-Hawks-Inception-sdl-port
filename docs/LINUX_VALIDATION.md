# Linux test status

The Nix build and asset-independent checks pass on x86_64-linux.
Three tests with deliberately delayed simulated input also passed. A full-suite
run encountered timeouts and did not finish; aarch64-linux is untested.
These checks do not establish manual playthrough or display/audio fidelity.

## Automated input fixes

Linux testing exposed timing failures in tests that simulate keyboard input.
The test changes pace input, retry discarded keys, and check game-loop progress
rather than temporary screen layouts. No production gameplay code was changed.
It is not established whether these failures also occur on other platforms.

Local-data tests run serially because they share writable data and have timing
deadlines. See [README.md](../README.md#linux-with-nix) for test commands.
