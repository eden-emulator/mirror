# PowerPC 64 backend

The ppc64 backend currently only supports the little endian variant, with big endian support being experimental for the time being. Additionally only A32 is supported (for now).

- Flag handling: Flags are emulated via software, while there may be some funny tricks with the CTR, I'd rather not bother - plus it's widely known that those instructions are not nice on real metal - so I would rather take the i-cache cost.
- 128-bit atomics: No 128-bit atomic support is provided, this may cause wrong or erroneous execution in some contexts.

To handle endianess differences all 16/32/64-bit loads and stores to the "emulated memory" are byteswapped beforehand.
