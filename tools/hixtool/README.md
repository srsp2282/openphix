# hixtool

Unpacks and decodes the data image (`ExtFlashDat.bin`, or a flash dump)
of the Autophix DM100 / DM300 based VAG scan tools. Python 3, no
dependencies (Pillow is used for PNG export when present, else PPM). The
formats are documented in `docs/data-image-format.md`; this tool is their
reference implementation and the yardstick for the firmware's C readers.

```
./hixtool.py ls        image.bin                  # files, nested containers included
./hixtool.py extract   image.bin outdir [-r]      # write every file (and nested ones)
./hixtool.py pack      indir image.bin [--encrypt]
./hixtool.py decrypt   in.bin out.bin [-a offset] # the cipher alone
./hixtool.py encrypt   in.bin out.bin [-a offset]

./hixtool.py strings   image.bin [-l _en] [-g 0x0102] [-s] [-i 0x1020019]
./hixtool.py font      image.bin WESTISO_16X16.bin "AÄ"
./hixtool.py images    image.bin outdir
./hixtool.py exp       image.bin [-g group] [-i id]
./hixtool.py cmd       image.bin [-g group] [-i id]
./hixtool.py menu      image.bin
./hixtool.py funcfg    image.bin [-m module -k kind [-v variant]]
./hixtool.py modules   image.bin [-a address] [--ev] [--codes]
./hixtool.py dstext    image.bin [-s TEXT -k key] [-l]
./hixtool.py udsdtc    image.bin [-s EV_ECM -k key] [-l]
./hixtool.py spfunc    image.bin [-f function]
./hixtool.py feedback  Feedback.bin [--profile {legacy,ad410}] [-v]
```

Every command accepts an encrypted or a plain image; the cipher is
detected and removed on the fly. `image.bin` may be a raw 32 MiB flash
dump (`openphix-tool read-flash`) or a package `ExtFlashDat.bin`.

`feedback` defaults to the older `legacy` payload profile.
`--profile ad410` selects the separately recovered ANCEL AD410 Feedback
table and parses its `Ask:` / `Ans:` bus events and CAN receive batches.
The AD410 profile is capture-validated rather than auto-detected; do not
assume it applies to other models without testing.

The package can also be used as a library:

```python
from hixtool import cipher, container, strings
plain = cipher.decrypt(open("extflash.bin", "rb").read())
files = {e.name: blob for e, blob in container.files(plain)}
en = strings.load(files["STRING.BIN"])["string_03_en.hix"]
print(en.get(0x1020019))        # 0019-Gateway
```

Modules: `cipher`, `container`, `table` (the shared indexed table
layout), `strings`, `fonts`, `images`, `exp`, `cmd`, `menu`, `funcfg`,
`sysscan`, `pairs` (DsTransID and UDSDTCTransID), `spfunc`, `feedback`,
`feedback_ad410`.
