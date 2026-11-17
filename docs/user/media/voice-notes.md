---
title: Record and browse Voice Notes
description: Record local voice notes, process them, and build pads to browse saved notes.
---

Voice Notes records audio from the device microphone and stores WAV files on
the SD-primary storage. You can manage recordings in the portal or build pads
to record, browse recent notes, and display saved details. Transcription and
title and summary generation are optional cloud services; recording itself does
not require them.

Voice Notes requires supported firmware with microphone input and SD-primary
storage. An SD slot alone is not enough: install a supported SD-primary
firmware variant and insert a readable card. The List and loaded-note workflow
also requires a display and PSRAM. Controls appear only when the installed
firmware supports them. Check the [hardware reference](../hardware.md) for
connections and storage requirements.
Back up existing recordings before changing variants; switching storage does
not transfer them automatically.

## Record and manage voice notes

Choose **Voice notes > Recording** when adding an action to a button or another
action host. The default command, **Toggle recording (start/save)**, starts a
new recording when idle. Press the same button again to stop and save it.
Toggle also saves a paused recording.

Use **Start recording**, **Pause recording**, **Resume recording**,
**Stop and save recording**, or **Discard recording** for separate controls.
Commands are rejected while the recorder is starting, saving, or discarding;
wait for the transition to finish before pressing again.
Save completes before subsequent actions run;
failed commands stop the remaining actions. Pause retains the microphone and
does not add silence to the recording. The microphone level meter is unavailable
while recording or paused.

Open **Voice Notes > Notes** to play, seek, download, or delete saved WAV files.
Notes are grouped by date; recordings made before the device clock is set appear
under **Unknown date**. Delete requires confirmation. The portal does not use
your browser's microphone. Recordings use the device input's native 16-bit PCM
format and stay on its selected storage backend. Free space limits recording
time; the device saves captured audio when it reaches the storage reserve.

The latest saved ID remains available until the device restarts. A reboot,
firmware update, factory reset, or device shutdown discards an unfinished
recording. If a manual update or reset reports that recording is stopping,
retry the operation. If microphone cleanup fails, retry **Start** after the
issue clears. On a pad, `[voice_note:last_saved.id]` shows the latest saved ID
and `[voice_note:recording.error|]` shows a recorder error when present.

## Process saved notes

To process notes, open **Voice Notes > Settings**. Configure the Transcription
Service with an Azure resource endpoint, deployment name, and API key. To
generate titles and summaries from saved transcripts, also configure the LLM
Service with a deployment that supports strict structured outputs. For each
service use the HTTPS resource root, such as
`https://<resource>.openai.azure.com/`, without an `/openai/` path, project URL,
or query. Select **Save**; a reboot is not required. Leave either key blank on
later saves to keep its stored value.

Enable **Automatically process saved notes** to include existing and future
notes with missing results. Automatic processing waits for Wi-Fi and other
required resources; it does not connect Wi-Fi or keep the device awake. It
retries each failing service after 30 seconds, 1 minute, and 5 minutes, then
pauses automatic retries. Save another recording or start processing manually
to begin a fresh cycle. Changing that service's settings also resets its cycle.
Provider cooldowns remain in effect; new recordings do not clear access or
configuration errors. Correct service settings if processing is blocked.

Alternatively, add a **Voice notes > Process saved notes** action to start a
best-effort pass without enabling automation. An offline or busy press schedules
nothing; press it again when the device is ready. This action starts processing
and does not wait for results to finish.

Processing pauses for two seconds after each operation before continuing,
giving other device tasks time to run. The overall pass still shows as running.
Recording takes priority over processing.

In **Notes**, select **Process note**. Open **View details** to play the note and
read its summary and transcript. Transcription sends the recording to Azure;
title and summary generation sends the saved transcript. Keep recordings,
downloads, and credentials protected. A failed attempt leaves saved results
intact. Use **More actions** in details to replace a transcript or regenerate a
title and summary. These actions may incur service charges; retranscribing does
not update an existing title or summary.

Access and deployment errors direct you to check Voice Notes Settings. Upload
limit errors require a smaller recording; rate or quota errors require checking
the Azure quota or retrying later. An empty transcription response is saved
successfully as **No speech detected**. Enrichment follows the normal flow and
may produce an empty summary. Completed results are not automatically retried;
use **Transcribe again** to try again.

## Build recording, list, and detail pads

This example uses three saved pads. The recording pad starts and saves audio
and shows background activity. The List pad shows recent saved notes. Tapping
a row loads that note into a shared snapshot and opens the detail pad, where
labels display previews and a Text reader displays the complete saved text.

Use **Pads > Pad Editor** to create and save pads named, for example,
**Recording**, **Notes**, and **Note detail**. Give the List and detail pads
enough space for a List or multiple labels and a back button. Each pad saves
independently; see [edit and save a pad](../pads/editing.md).

### Configure the recording pad

1. On **Recording**, add a button with the tap action **Voice notes >
   Recording** and command **Toggle recording (start/save)**.
2. Set its labels to `Record / Save`, `[voice_note:recording.state]`, and
   `[voice_note:recording.duration_s] s`. The state changes as the recorder
   starts, records, and saves.
3. Add a status button without actions. Use labels such as
   `Processing: [voice_note:processing.state]`,
   `Transcription: [voice_note:transcription.state]`, and
   `Summary: [voice_note:enrichment.state]`.
4. Add another button with **Navigate to screen** targeting **Notes**.
   Optionally add a button with **Voice notes > Process saved notes** for a
   manual processing pass. Select **Save Pad**.

The overall processing state is `idle`, `waiting`, `running`, or `blocked`.
Transcription and enrichment states describe the background services, not the
selected List item. Their `note_id`, `error`, and `retry_status` bindings can
provide more detail; find the available keys in the Binding reference.
Retry status is empty before retries, then shows **Retry 1 of 3**,
**Retry 2 of 3**, **Retry 3 of 3**, or **Retries halted**. It describes the
service's retry cycle, not an individual note, and resets on reboot.

### Configure the recent-notes pad

1. On **Notes**, choose **List** for a button's content. Keep its generated
   **List ID** and choose **Recent voice notes** (`[voice_note:recent_notes]`)
   as the source.
2. Set the top item label to `[item:title|Untitled]` and the bottom label to
   `[expr:duration([item:duration_ms],"ms")]`. Titles are available after successful enrichment;
   the fallback also works without cloud processing.
3. Under **Item tap actions**, add **Voice notes > Load saved note** with
   **Note ID** set to `[item:id]`. Add **Navigate to screen** targeting
   **Note detail** as the next action, in that order.
4. Add a separate **Navigate back** button and select **Save Pad**.

The source contains up to 20 recent notes, newest first. It also exposes
`id`, `created_at`, `duration_ms`, `has_transcript`, and `has_enrichment`.
`created_at` is a Unix timestamp in seconds and is unavailable if the clock
was not set. Format it with `[expr:datetime([item:created_at])|Time unavailable]`.
See [expressions](../pads/expressions.md#format-dates-and-durations) for more
formats, and the [List guide](../widgets/list.md) for row layout and item actions.

### Configure the detail pad

1. On **Note detail**, add labels for `[voice_note:loaded.status]` and
   `[voice_note:loaded.title|Untitled]`. Add a button with **Text reader** content
   and set **Text** to exactly `[voice_note:loaded.transcript|No transcript]`
   to read the complete transcript. Use a second reader with
   `[voice_note:loaded.summary|No summary]` for the full summary, if needed.
   Configure scrolling or paging using the
   [Text reader guide](../widgets/README.md#text-reader).
2. Add a label for `[voice_note:loaded.error|]` and a **Navigate back** button.
   Select **Save Pad**.
3. Show **Recording** on the device. Tap the record button, speak, and tap it
   again to save. Open **Notes**, then tap the new row. The detail pad opens
   while loading continues: status changes from `loading` to `ready` or `error`.

The Load action captures the row's ID before navigation; `[item:id]` is only
available during the row's immediate action. Missing titles, summaries, or
transcripts use the configured fallbacks. For playback, seeking, and reading
in the portal, use **Voice Notes > Notes > View details**.

## Show a loaded voice note on a pad

The loaded ID, creation time, duration, and transcript/enrichment availability
are also available in the Binding reference. A rejected ID stops the action
list; later read errors appear in `[voice_note:loaded.status]` and
`[voice_note:loaded.error|]` instead. Loading defers while recording or a
firmware update needs the device.

Use labels for details that must not show a previous note. Some widgets retain
their last value while a binding is unavailable or animate from that value;
historical charts can also include samples from previous notes. Check the loaded
status and ID before relying on widget values.

One loaded note is shared by all pads. Its saved title, summary, and transcript
remain unchanged until another Load, a matching **Delete saved note** action,
or reboot, even if the source note is edited or deleted through the portal.
Labels show short UTF-8-safe previews; use a
[Text reader](../widgets/README.md#text-reader) for complete text on the device.
Checked action fields reject values that do not fit rather than using a
truncated prefix.

## Delete a note from a List

Add **Voice notes > Delete saved note** to a List item action and set **Note ID**
to `[item:id]`. Deletion is permanent and also removes the note's transcript
and enrichment. The action waits for deletion to finish; if the note is busy
or deletion fails, later actions do not run. To ask for confirmation, capture
`[item:id]` with **Set variable**, then add **Ask for confirmation** and
**Delete saved note** using `[var:name]`. The List refreshes after deletion.
See [runtime variables](../pads/actions.md#set-a-runtime-variable).

When the action deletes the loaded note, its details clear and loaded status
returns to `idle`. Deleting a different note leaves the loaded details unchanged.
Deleting through the web portal does not clear the loaded snapshot.

## Voice Notes reference

The action picker groups these controls under **Voice notes**:

| Action                | Purpose                                                |
| --------------------- | ------------------------------------------------------ |
| Recording             | Toggle, start, pause, resume, save, or discard capture |
| Process saved notes   | Start a background pass for missing results            |
| Load saved note       | Request a shared detail snapshot by note ID            |
| Delete saved note     | Permanently delete a note and its saved results        |

Load requires a display and PSRAM. Load and Delete accept a note ID or a
binding such as `[item:id]`. See the [pad walkthrough](#build-recording-list-and-detail-pads)
and [deletion instructions](#delete-a-note-from-a-list) for ordering and
confirmation.

Scalar bindings use `[voice_note:key]`. The keys fall into these groups:

* `recording.*` and `last_saved.id`: recorder state, duration, scenario,
  error, and the last saved ID; for example, `[voice_note:recording.state]`
* `processing.state`: overall background activity, such as
  `[voice_note:processing.state]`
* `transcription.*` and `enrichment.*`: service state, latest attempted note
  ID, error, and retry status; for example, `[voice_note:transcription.error|]`
* `loaded.*`: the shared loaded note's status, error, ID, metadata, and text
  previews; for example, `[voice_note:loaded.title|Untitled]`

In a bindable field, open **fx** and search the **Binding Reference** for
`voice_note` to see the complete keys supported by your firmware. The
`loaded.*` keys require a display and PSRAM. See
[binding syntax and fallbacks](../pads/bindings.md#insert-and-verify-a-token).

`[voice_note:recent_notes]` is a List source, not a scalar binding. Its row
fields are `id`, `title`, `created_at`, `duration_ms`, `has_transcript`, and
`has_enrichment`; use them as `[item:field]` in row labels or immediate item
actions. The [recent-notes example](#configure-the-recent-notes-pad) explains
the optional fields and timestamp format.

[Media tasks](README.md) | [User guides](../README.md)
