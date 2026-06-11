# Plano: Fix GSP_INIT_DONE

## Problema

Após SEC2 boot + pre-boot RPCs (SET_SYSTEM_INFO + SET_REGISTRY) + doorbell, o GSP
RISC-V nunca envia o evento `NV_VGPU_MSG_EVENT_GSP_INIT_DONE` (0x1001). A polling
loop em `bootGSP()` expira após 60s com `initDone=false`.

## Causas Identificadas

### 1. Doorbell: valor errado (writePtr em vez de 0)

**Ficheiro:** `Src/GA104Device.cpp:1132`

```cpp
uint32_t dbVal = fCmdqTx ? fCmdqTx->writePtr : 2;
writeReg32(GSP_DOORBELL_REL, dbVal);  // ❌ escreve 2
```

**Referência NVIDIA:** `NV_PGSP_QUEUE_HEAD(i) = 0` — o valor é um **pulse**,
não importa o valor. Mas o HW pode interpretar valores > 0 como outra operação.

**Correção:** `writeReg32(GSP_DOORBELL_REL, 0)` (igual ao `sendGspRpc()` linha 2000).

### 2. Polling loop sai cedo com CMDQ readPtr

**Ficheiro:** `Src/GA104Device.cpp:1209-1212`

```cpp
if (cmdqRp > 0) {
    IOLog("GA104: CMDQ processed after %dms! rPtr=%u\n", waitMs, cmdqRp);
    break;  // ❌ sai antes do INIT_DONE
}
```

A firmware pode processar os RPCs de pre-boot (incrementando readPtr) mas ainda
não ter completado a inicialização do RM para enviar INIT_DONE. O loop sai cedo,
e depois `sendGspRpcAllocRoot()` tenta enviar ALLOC_ROOT sem o RM estar pronto.

**Correção:** Remover este `break`. Só sair do loop por:
- `INIT_DONE` recebido (`initDone == true`)
- Timeout (60s)

### 3. RPCs de pre-boot podem não estar visíveis na VRAM

**Ficheiro:** `Src/GA104Device.cpp:1051-1057`

As entradas CMDQ são escritas em `fCmdqEntryBase` (sysmem) e depois copiadas
para VRAM via `wrVRAM()`. No entanto:
- `wrVRAM()` usa `fBar1Phys + vramOff` — `vramOff` pode ter cálculo errado
- Se `wrVRAM()` falhar, a firmware (que lê da WPR2 VRAM) nunca vê os comandos

**Verificação:** Confirmar que `fVramLayout.queuePhysAddr` está correto e que
a VRAM contém as entradas CMDQ após a cópia.

### 4. WPR Meta não contém LibOS phys address correto

**Ficheiro:** `Src/GA104Device.cpp` — `populateWprMeta()`

SEC2 precisa do endereço físico do WPR Meta nos MAILBOX0/1. O WPR Meta contém
ponteiros para o bootloader, Radix3, assinatura SEC2. Se algum destes estiver
errado, SEC2 pode não carregar a firmware corretamente.

**Verificação:** Comparar com o dump de `sec2_analysis.txt` no diretório dev/.

## Passos de Correção

1. **Corrigir doorbell SEC2 path**: `bootGSP()` linha 1132 → `writeReg32(GSP_DOORBELL_REL, 0)`

2. **Remover break precoce por CMDQ readPtr**: `bootGSP()` linhas 1209-1212 → remover

3. **Verificar bootSEC2() exit state**: Confirmar que GSP CPUCTL não está HALTED
   e que o RISC-V ACTIVE_STAT está set após SEC2 terminar. Adicionar log:
   `RISCV_CPUCTL` (0x111388) bit 7 = ACTIVE_STAT

4. **Corrigir WPR2 base se necessário**: O polling loop lê `fShmBuf` (sysmem) para
   MSGQ writePtr. Se a firmware escreve na VRAM em vez de sysmem, o host nunca vê.
   Garantir que `sharedMemPhysAddr` no RMARGS aponta para sysmem (já está, linha 1774).

5. **Verificar ordem IRQ routing**: IRQ deve ser configurado ANTES de escrever
   RPCs e doorbell. Actualmente está OK (linhas 1111-1123 antes de 1132).

## Prioridade

1. **Crítico**: Item 1 (doorbell) + Item 2 (break precoce)
2. **Investigação**: Item 3 (VRAM visibility) + Item 4 (WPR Meta)
3. **Teste**: Compilar, carregar, verificar logs "GSP_INIT_DONE received"
