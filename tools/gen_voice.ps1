# Synthesises the fixed status phrases in tools/voice_phrases.json with a local Windows
# voice and packs them into firmware/answer_demo/voice_clips.h (16 kHz, 16-bit, mono).
# The generated header is git-ignored: the output of a system voice may carry licence
# restrictions, so it is not committed. Without the header the firmware builds silent.
#   powershell -NoProfile -File tools/gen_voice.ps1 [-Voice "Microsoft Haruka Desktop"] [-Rate 0]
param(
  [string]$Voice = 'Microsoft Haruka Desktop',
  [int]$Rate = 0,
  [string]$Phrases = (Join-Path $PSScriptRoot 'voice_phrases.json'),
  [string]$Out = (Join-Path $PSScriptRoot '..\firmware\answer_demo\voice_clips.h')
)
Add-Type -AssemblyName System.Speech
$json = [IO.File]::ReadAllText($Phrases, [Text.Encoding]::UTF8) | ConvertFrom-Json
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('stackchan_voice_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $tmp | Out-Null
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$synth.SelectVoice($Voice)
$synth.Rate = $Rate
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(
  16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
foreach ($p in $json.PSObject.Properties) {
  $synth.SetOutputToWaveFile((Join-Path $tmp ($p.Name + '.wav')), $fmt)
  $synth.Speak($p.Value)
  $synth.SetOutputToNull()
}
$synth.Dispose()
$order = ($json.PSObject.Properties | ForEach-Object Name) -join ','
& python (Join-Path $PSScriptRoot 'voice_pack.py') $tmp $order $Out $Voice
Remove-Item -Recurse -Force $tmp
