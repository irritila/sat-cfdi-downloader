# Inter SemiBold (T020 D11)

- Proyecto: Inter, https://github.com/rsms/inter
- Release: v4.1 (publicada 2024-11-16), la ultima estable al 2026-10-26.
- Origen: https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
  - SHA-256 del zip: `9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e`
- Archivos versionados (sin modificar):

| Archivo | Ruta en el zip | SHA-256 |
| --- | --- | --- |
| `Inter-SemiBold.ttf` | `extras/ttf/Inter-SemiBold.ttf` | `78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3` |
| `OFL.txt` | `LICENSE.txt` | `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a` |

Licencia: SIL Open Font License 1.1 (`OFL.txt`). Solo se usa para convertir el
wordmark a trazos (`tools/branding`); la app no la carga ni la empaqueta.

Comprobar: `shasum -a 256 resources/branding/fonts/Inter/*`.
