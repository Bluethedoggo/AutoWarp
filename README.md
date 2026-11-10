# AutoWarp

**Warp e quantização automáticos para samples, loops e músicas.**
Criado por **DogSide Productions** e **Bluethedoggo**.

Você joga um áudio no plugin e ele (1) acha os "cortes" nos transientes, (2) descobre quantos beats o loop tem, e (3) encaixa tudo na grade do projeto, igual ao *Quantize* do piano roll do FL Studio, só que no áudio. Você pode mexer nos cortes à mão, como num programa de warping.

## O que ele faz

- **Auto-Warp**: detecta transientes, define o tamanho do loop em beats e o tempo original.
- **Quantize** com força (0–100 %), grade (1/4, 1/8, 1/16, 1/32, tercinas 1/8T e 1/16T) e **swing**.
- **Cortes editáveis**: adicione, mova e apague marcadores; arraste o marcador de baixo para posicionar um hit manualmente (sem snap com Shift).
- **START / END** arrastáveis para escolher a região do loop.
- **Warp modes**: *Beats* (mantém o pitch, preserva o ataque dos hits) e *Re-Pitch* (varispeed).
- **Segue o tempo do host**: o loop é re-renderizado se você mudar o BPM do projeto.
- **Playback**: *Host Sync* (toca travado na linha do tempo do host) ou *MIDI Trigger* (cada nota do piano roll dispara o loop do início).
- **Preview** para ouvir sem dar play no DAW, **Export WAV** do loop já warpado.
- O áudio, os cortes e as configurações são salvos **dentro do projeto** do DAW.
- Carrega WAV, AIFF, FLAC, MP3 e OGG (arrastar e soltar funciona).

## Como compilar o `.dll` (VST3) no Windows

O plugin é um **VST3**. No Windows, o `.dll` fica dentro da pasta `AutoWarp.vst3`
(`AutoWarp.vst3\Contents\x86_64-win\AutoWarp.vst3`). É isso que o FL Studio carrega.
Compile em Windows de um destes jeitos:

### Opção A: GitHub Actions (sem instalar nada)
1. Crie um repositório no GitHub e envie esta pasta.
2. Aba **Actions** → *Build AutoWarp (Windows VST3 / .dll)* → aguarde.
3. Baixe o artefato **AutoWarp-Windows-VST3** (contém `AutoWarp.vst3` e `AutoWarp-Standalone.exe`).
4. Antes de enviar a pasta ao GitHub, **apague a pasta `build`**, se existir.

### Opção B: no seu PC
1. Instale (grátis): Visual Studio 2022 Community com *Desktop development with C++*, [CMake](https://cmake.org/download/) e Git.
2. Dê dois cliques em `build_windows.bat` (baixa o JUCE sozinho).
3. O plugin fica em `build\AutoWarp_artefacts\Release\VST3\AutoWarp.vst3`.

## Instalar no FL Studio

> **IMPORTANTE:** a pasta **`AutoWarp.vst3`** precisa ficar exatamente em
> **`C:\Program Files\Common Files\VST3\`**. Fora dessa pasta o FL Studio pode não achar o plugin.

1. Descompacte o zip baixado.
2. Copie a pasta **`AutoWarp.vst3`** **inteira** (não só o arquivo de dentro, e sem pastas por fora) para:
   `C:\Program Files\Common Files\VST3\`
   O resultado final deve ser:
   `C:\Program Files\Common Files\VST3\AutoWarp.vst3\Contents\x86_64-win\AutoWarp.vst3`
3. Clique com o botão direito em `AutoWarp.vst3\Contents\x86_64-win\AutoWarp.vst3` → **Propriedades**. Se aparecer **Desbloquear**, marque e clique em OK (o Windows bloqueia arquivos baixados da internet).
4. FL Studio → **Options → Manage plugins**, marque **Verify plugins** e clique em **Find installed plugins**.
5. Procure **AutoWarp** em **Plugin database → Installed → Generators** e adicione ao Channel Rack.

### Problemas comuns
- **Não aparece no FL:** confira o caminho do passo 2, faça o passo 3 (desbloquear) e escaneie de novo (passo 4).
- **O Windows Defender acusou o `AutoWarp-Standalone.exe`:** é um falso positivo comum em programas novos sem assinatura digital. Você **não precisa** do `.exe` para usar o plugin no FL; pode apagá-lo. Se quiser conferir, envie o arquivo em virustotal.com.
- **"Não é um .dll":** no Windows, o VST3 é uma DLL com a extensão `.vst3`. O FL Studio só reconhece com essa extensão. O formato VST2 (`.dll` solto) não é suportado.

## Como usar
1. **LOAD** (ou arraste um áudio para a janela). O Auto-Warp roda sozinho ao carregar.
2. Em cima fica o áudio original com os cortes (laranja). Embaixo, o loop já alinhado na grade.
3. Ajuste **QUANTIZE** (força), **Quantize grid** e **SWING**.
4. Se o tamanho do loop saiu errado, use **/2**, **x2** ou digite os beats em *Loop length (beats)*.
5. Refine os cortes:

| Ação | Como |
|---|---|
| Adicionar corte | duplo clique na faixa de cima |
| Mover corte | arrastar o marcador de cima |
| Apagar corte | clique direito (ou Alt+clique) no marcador |
| Colocar um hit em outro lugar | arrastar o marcador de **baixo** (Shift = sem snap na grade) |
| Voltar o marcador ao automático | clique direito no marcador de baixo |
| Mudar início / fim do loop | arrastar as linhas verdes START / END |
| Zoom / rolagem | roda do mouse / Shift + roda; **FIT** reseta |

6. **PREVIEW** toca o loop sem o DAW. **EXPORT WAV** salva o resultado.

> Para usar como o Quantize do piano roll: deixe *Playback = Host Sync*, **QUANTIZE = 100 %**, grade em 1/16 e dê play.
> Para tocar com notas: *Playback = MIDI Trigger* e escreva as notas no piano roll.

## Estrutura

```
Source/WarpEngine.*      motor (detecção, quantização, time-stretch), sem dependência do JUCE
Source/PluginProcessor.* parâmetros, thread de render, reprodução, estado do projeto
Source/PluginEditor.*    janela principal
Source/WarpView.*        forma de onda + cortes editáveis
tests/engine_test.cpp    teste automático do motor (g++ -O2 -std=c++17 -I../Source engine_test.cpp ../Source/WarpEngine.cpp)
```

## Créditos e licença
Criado por **DogSide Productions** e **Bluethedoggo**. Os créditos aparecem no rodapé da janela, no botão
**ABOUT** e como fabricante nos metadados do plugin. Veja `LICENSE.txt`: o plugin usa o framework JUCE
(AGPLv3 ou licença comercial); se for distribuir o binário, respeite a licença do JUCE.
