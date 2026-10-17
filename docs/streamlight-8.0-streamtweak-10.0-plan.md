# StreamLight 8.0 + StreamTweak 10.0 — decisioni di progetto

Stato: **deciso, sviluppo non iniziato** (ottobre 2026). Lo stesso file è in entrambi i repo
(`StreamLight/docs/` e `StreamTweak/docs/`): se ne cambi uno, aggiorna l'altro.

Mockup interattivo di riferimento (due tavole sulla stessa tela, privato):
https://claude.ai/artifact/US3TPV2crWdUB4HRVw6VRM — i sorgenti sono in `docs/plan-8.0-10.0-mockup/`
(formato `.dc.html` della tela: si aprono dal link, non da soli nel browser).

## L'idea in una riga

Tutto resta **sull'host** (dati, lavoro, servizio), ma l'interfaccia di uso quotidiano passa in
**StreamLight**, in una nuova sezione **Host** aperta da un quarto pulsante sul riquadro dell'host.
StreamTweak tiene solo ciò che si configura **una volta**, sull'host. Il bridge resta.

Regola che decide dove va una cosa: **configurazione una tantum → StreamTweak sull'host; uso di tutti i
giorni → StreamLight dal divano.**

## StreamLight 8.0 — sezione Host

### Accesso
- Quarto pulsante **Host** sul riquadro dell'host, dopo *Open · Profiles · Options* (`HostStage.qml`,
  lista `actions`), con badge NEW.
- Sempre visibile, mai nascosto (regola "mostrato in grigio, non tolto"):
  - StreamTweak non trovato → in grigio, motivo sulla riga sotto;
  - in attesa / negato → si apre su una pagina di accesso unica (PIN in attesa, oppure *Request access
    again*), senza schede;
  - autorizzato → la sezione con le schede.

### Struttura
- Pagina a schede **come Impostazioni**: `SectionTabBar`, LB/RB per le schede, stick destro per
  scorrere, barra dei comandi in basso cliccabile. Tutto navigabile col pad.
- Schede: **Dashboard · Sessions · Library · Apps · NVIDIA** (NVIDIA solo se l'host ha una GPU NVIDIA).
- Opzionale, rimandabile: una scheda *Funzioni* con gli interruttori per host oggi in
  *Impostazioni → StreamTweak*.

### Dashboard — l'host a riposo
- **Niente grafici live della sessione** in StreamLight: se lo stream è attivo, sul client si vede lo
  stream. Sparisce il cambio di stato "streaming on/off" della vecchia Dashboard di StreamTweak.
- Numeri dell'host a riposo da `STATS` (già esistente, nessun comando nuovo): GPU %, temperatura GPU,
  encoder, VRAM, CPU, rete TX. Una richiesta al secondo **solo mentre la scheda è visibile**.
- Mini-linea degli ultimi 60 s sotto ogni numero, tenuta in memoria dal client (nessuno storico
  sull'host). Opzionale.
- Se l'host sta già trasmettendo a un altro dispositivo: una riga in alto
  ("In streaming su *Steam Deck* · Hades II").
- Riquadro **Rete**: velocità attuale e originale, interruttore del **permesso dell'host**
  (`AllowClientControl`), ripristino. Niente IP Tailscale (StreamLight lo mostra già).
- Riquadro **Audio spaziale**: interruttore on/off (`SpatialAudioEnabled`), stato in sola lettura
  (formato, dispositivo, attivo o no), "vale dalla prossima sessione"; in grigio con motivo se il
  formato non è disponibile. Dispositivo e formato si scelgono sull'host.
- Riga **Windows Update** in fondo, che apre la finestra di avanzamento già esistente.

### Sessions
- Elenco con filtro *Questo dispositivo / Tutti* (l'host può associare la sessione al client perché
  `SESSIONDATA` arriva firmato da `AUTH1`). Le sessioni di client non-StreamLight compaiono come
  "client sconosciuto".
- Report: voto, i quattro controlli, timeline **una corsia alla volta** — LB/RB corsia, D-pad cursore,
  LT/RT zoom.
- Confronto: X segna due sessioni, poi *Compare*.
- `SESSIONDATA` continua a partire durante lo stream: spariscono i grafici live, non la raccolta.

### Library — solo informativa
- Conteggi, store, ultimo sync, elenco giochi in sola lettura. **Non si avviano giochi da qui.**
- **Sync and restart**: sync della libreria, poi riavvio automatico del server di streaming
  (Vibeshine / Vibepollo / Sunshine / Apollo) perché l'elenco venga applicato.
  - Il riavvio lo fa il servizio LocalSystem, **solo sui nomi fissi dei servizi** dei quattro server
    (da verificare per ciascun fork).
  - **Rifiutato se c'è uno stream attivo da qualunque client.**
  - StreamLight mostra i passi (scansione → riavvio → attesa che l'host risponda).
  - ⚠️ Oggi il sync manuale **svuota le esclusioni** (`GameLibraryService.cs:54-55`): il sync chiesto
    dal client deve **rispettarle**.
- Aggiunte manuali, esclusioni e tile restano sull'host.

### Apps
- Solo gli interruttori *gestita / non gestita* sull'elenco fatto sull'host (l'interruttore per app
  esiste già). **Non si aggiungono app dal client.** Nessun percorso passa sul bridge.

### NVIDIA
- Informazioni (GPU, driver, versioni DLSS, impostazioni del profilo salvato) + interruttore di
  **Sentinel** (auto-restore). In grigio se sull'host non c'è un profilo salvato.
- Snapshot, ripristino e svuotamento solo sull'host.

### Options del riquadro host — cosa esce
- **Windows Update** → riga della Dashboard (stesse condizioni: online, accoppiato, autorizzato).
- **Request Access** → pagina di accesso della sezione Host.
- **Match Link Speed / Restore NIC Speed** → riquadro Rete della Dashboard, **con entrambe le
  direzioni** e i quattro motivi di grigio (in cambio, rifiutato dall'host, matching spento, host
  occupato). Il mockup mostra solo il ripristino.
- Dopo: Options = l'host come server di streaming (Tailscale, Wake, Test Network, PyroWave Test,
  Details, Rename, Background, Delete). Verificare il layout della griglia, oggi pensata "a nove"
  (`menuItemsFor` in `HomeScreen.qml`).
- *Installa gli aggiornamenti prima dello spegnimento* resta nella finestra Power (X).
- Facoltativo, solo in 8.0: una riga in Options "Windows Update e la velocità di rete ora sono in Host".

## StreamTweak 10.0 — l'host

### Cosa resta e cosa sparisce
- **Restano**: Status, Clients, Apps, Library, Spatial audio, NVIDIA Sentinel, Settings.
- **Spariscono**: Dashboard, Sessions, Video/HDR (`HdrService`, `DisplayViewModel`).
- Il bridge resta, e resta nella sessione utente: appunti, audio, finestre dei giochi e stato di
  blocco non si vedono dalla sessione 0 del servizio.
- Lo storico delle sessioni continua a essere **registrato sull'host**; si guarda da StreamLight.

### Status — la pagina d'apertura
- **Needs you** in alto, solo se c'è qualcosa: richiesta di un client con PIN e
  *Deny / Approve / Approve with host management*; problemi risolvibili solo sull'host (porta del
  bridge chiusa nel firewall, Sentinel armato senza profilo…).
- **Streaming now** con **Return to client** (deciso: si tiene qui, con conferma).
- Lista di controllo, una riga per componente con pallino verde/ambra/rosso, ognuna porta alla sua
  pagina: servizio, bridge, server di streaming, libreria, app gestite, audio spaziale, rete, Sentinel.
- Nota fissa: Dashboard e Sessions sono in StreamLight 8 (pulsante Host); lo storico resta registrato qui.
- **Primo avvio**: la stessa pagina fa da configurazione guidata in 4 passi (server trovato, porta
  aperta, primo client approvato, app scelte). Nessuna procedura guidata separata.

### Clients
- Approvazione con PIN **sull'host** (radice della fiducia).
- Per client un interruttore **Host management**: chi ce l'ha può cambiare le impostazioni dalla
  sezione Host; chi non ce l'ha tiene tutte le funzioni accoppiate ma guarda soltanto.

### Apps
- *Add app* sceglie tra i **programmi in esecuzione** (nessun selettore di file libero). Si toglie da qui.
- Gestita o no si decide da StreamLight.

### Library
- Schede **In Vibepollo / Excluded / Added by hand**.
- Ogni gioco con **copertina piccola**, **badge SVG dello store** (`Resources/Badges/store_*.svg`), il
  **comando esatto** scritto nel server di streaming, data, **Play** ed **Exclude**.
- **Exclude** sposta il gioco negli esclusi, **Include again** lo riporta. Un escluso resta fuori a ogni
  sync, chiunque lo avvii.
- **Play** si tiene come strumento di test. Oggi le regole del comando sono duplicate:
  `SunshineSync.BuildLaunchCommand` (verso `apps.json`) e `GameLibraryViewModel.LaunchGame` (Play);
  in comune c'è solo `StoreLaunchUri` (Epic). **In 10.0 Play legge `cmd`, `detached` e `working-dir`
  dalla voce già scritta in `apps.json` ed esegue quelli**, così prova davvero ciò che lancerà il
  server e sparisce la seconda copia delle regole.
- Restano qui: sync manuale, riavvio dopo il sync, aggiunte a mano, sostituzione delle tile.

### Spatial audio
- Si tiene. Pagina ridotta: formato (Dolby Atmos for Headphones / Windows Sonic) e dispositivo con
  disponibilità; l'on/off è anche nella Dashboard di StreamLight.

### NVIDIA Sentinel
- Gestione completa qui: snapshot, ripristino, svuotamento, auto-restore, registro dei ripristini.

### Settings
- Avvio con Windows, permesso sulla velocità di rete, appunti condivisi, "solo sessioni con un gioco",
  server di streaming rilevato, bridge e cartella dei log.

## Bridge

- Si tiene così com'è; i comandi esistenti restano per i client vecchi.
- Nuovi comandi: per ogni scheda **una lettura e un'impostazione**; l'host è l'unica fonte di verità,
  chi salva ricarica lo stato.
- `CAPS` dice quali schede esistono (niente NVIDIA su GPU non NVIDIA, niente Sessions su host vecchi).
- Elenchi e timeline spediti **a pezzi** (il bridge è a righe di testo).
- **Sicurezza**, nella linea della 9.2.5: dal client passano solo letture, interruttori e azioni fisse
  (sync con riavvio, ripristino della rete). **Nessun percorso, nessun comando che lanci programmi.**
  I comandi che scrivono richiedono la firma e il permesso *Host management* del client.
  `SECURITY.md` va aggiornato.

## Già esistente che si riusa
- StreamLight: `SectionTabBar`, `SegmentedSelector`, `OnOffSelector`, `PadScroll`, `NavigableDialog`,
  la finestra di avanzamento di Windows Update, `HostMetricsPoller` (`STATS`).
- StreamTweak: riquadro "host setup" ed elenco client della Dashboard attuale (`HomeView`) per la pagina
  Status; `AllowClientControl`; l'interruttore per app; `QualityGradeCalculator`.

## Fasi

1. **StreamLight 8.0**: pulsante Host, pagina di accesso, Dashboard, Apps, NVIDIA; spostamento dei tile
   da Options. Comandi semplici.
2. **Library**: sync con riavvio del server (la parte più delicata lato host), rispetto delle esclusioni.
3. **Sessions**: elenco, report con la timeline per il pad, confronto.
4. **StreamTweak 10.0**: pagina Status, Clients con *Host management*, Library rinnovata con Play da
   `apps.json`, Apps da programmi in esecuzione; via Dashboard, Sessions, Video/HDR.

Ogni fase si pubblica da sola; finché la fase 4 non esce, l'interfaccia vecchia di StreamTweak c'è ancora.

## Da verificare prima di scrivere codice
- Nomi dei servizi Windows di Sunshine, Apollo, Vibeshine e Vibepollo, e come riavviarli in sicurezza.
- Comportamento di Vibepollo se `apps.json` cambia mentre gira (serve davvero il riavvio? sì per oggi:
  StreamTweak scrive "Restart … to apply").
- Che il bridge risponda con l'host alla schermata di blocco (utile per Windows Update da remoto).
