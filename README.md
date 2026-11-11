# Mini DayZ per PS Vita

Porting nativo per PS Vita di **Mini DayZ** (la versione browser/mobile di Bohemia Interactive, build *Plus 1.2* dal repository [NextDev65/MiniDayZ](https://github.com/NextDev65/MiniDayZ)).

Il gioco originale è un export HTML5 di Construct 2. Invece di usare il browser della Vita (troppo lento e senza WebGL), questo progetto lo esegue in un runtime nativo:

- **QuickJS** esegue il codice originale non modificato (`c2runtime.js` + `data.js`) nella modalità "Ejecta" di Construct 2, cioè senza DOM;
- `src/js/prelude.js` fornisce il minimo di browser che serve: WebGL (tradotto su **vitaGL**), canvas 2D software per i testi, Web Audio / `<audio>` (Ogg Vorbis con stb_vorbis), XHR su file locali, localStorage e IndexedDB salvati su file, timer, eventi touch;
- DOMParser + XPath in puro JS (`src/js/xml.js`) per i testi localizzati del gioco.

## Installazione

1. Scarica `MiniDayZ.vpk` dalla scheda **Actions** (ultimo build riuscito → artifact `MiniDayZ-vpk`) oppure dalle **Releases**.
2. Installalo con VitaShell.
3. Serve `ur0:data/libshacccg.suprx` (compilatore di shader). Se non ce l'hai, estrailo con [ShaRKBR33D](https://github.com/Rinnegatamante/ShaRKBR33D). Se manca, il gioco lo segnala all'avvio.

Salvataggi, log e configurazione dei tasti si trovano in `ux0:data/MiniDayZ/`:
`storage.json` (salvataggi), `log.txt` (log per segnalare problemi), `controls.txt` (tasti).

## Comandi

| Tasto | Azione |
|---|---|
| Levetta sinistra / croce direzionale | Movimento |
| Levetta destra | Muove il cursore (menu, inventario) |
| R o L | Tocco / trascinamento sul cursore |
| Croce | Azione: raccogli, interagisci (icona mano) |
| Quadrato | Arma / attacco |
| Triangolo | Zaino |
| Cerchio | Diario |
| Start | Impostazioni / pausa |
| Select | Personaggio |

Il touch screen frontale funziona come nella versione mobile.
I tasti premono punti dell'interfaccia: se qualcosa non torna puoi cambiare le coordinate in `ux0:data/MiniDayZ/controls.txt` (schermo 960×544). Il file viene creato al primo avvio.

## Compilare

La build ufficiale avviene su GitHub Actions (`.github/workflows/build.yml`): scarica i dati del gioco, genera le immagini LiveArea e compila con l'immagine Docker `vitasdk/vitasdk`. Avviando il workflow a mano si può scegliere la versione del gioco (`plus`, `original`, `reloaded`).

In locale:

```sh
tools/fetch_game.sh plus          # scarica il gioco in ./game
python3 tools/make_livearea.py    # immagini LiveArea
docker run --rm -v "$PWD:/src" -w /src vitasdk/vitasdk sh tools/ci_build.sh
```

Esiste anche una build Linux di test (EGL headless, input da script, screenshot) per sviluppare senza console:

```sh
cmake -B build-linux -DMDZ_LINUX=ON && cmake --build build-linux
mkdir -p run/res run/save && cp src/js/*.js assets/fonts/*.ttf run/res/ && ln -s ../game run/game
cd run && MDZ_FRAMES=600 MDZ_SHOTS=599 ../build-linux/minidayz
```

## Note

- I dati del gioco non sono inclusi nel repository: vengono scaricati durante la build. Mini DayZ è © Bohemia Interactive.
- Prestazioni: il gioco è interpretato da QuickJS (niente JIT); la CPU viene portata a 444 MHz. Le prestazioni reali vanno verificate su console.

## Licenze

QuickJS-ng (MIT), stb (MIT/pubblico dominio), @xmldom/xmldom e xpath (MIT), font Liberation Sans (SIL OFL), vitaGL (LGPL-2.1). Vedi `third_party/` e `assets/fonts/`.
