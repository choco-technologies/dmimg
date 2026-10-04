# dmimg Documentation

dmimg is the DMOD image decoder interface: decoders of image formats are
plugins (dmf modules implementing the dmimg DIF), programs read images
through the dmimg API.

## Contents

- **[api-reference.md](api-reference.md)** - the API for programs and the DIF for decoders
- **[writing-a-decoder.md](writing-a-decoder.md)** - a decoder plugin, step by step

View documentation using `dmf-man`:

```bash
dmf-man dmimg          # Main documentation
dmf-man dmimg api      # API reference
```
