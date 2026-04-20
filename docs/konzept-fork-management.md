<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Konzept: Fork-Management für qvest-digital/mxl-dmf-demo

## Zusammenfassung

Der Fork `qvest-digital/mxl-dmf-demo` von `dmf-mxl/mxl` hat sich unkontrolliert entwickelt: 9 Branches mit teilweise redundantem Inhalt, keine automatische Synchronisierung mit Upstream, und Qvest-eigene Erweiterungen sind nicht sauber vom Upstream-Code getrennt. Dieses Ticket beschreibt die Schritte, um den Fork in einen wartbaren Zustand zu bringen.

---

## Ist-Zustand

### Upstream

- **Repo:** `dmf-mxl/mxl` (GitHub)
- **Build-System:** CMake + vcpkg, CI via GitHub Actions (`build.yml`, `backports.yml`)
- **Aktivität:** Aktive Entwicklung (Fabrics API, Bug Fixes, Workflow-Verbesserungen)

### Fork (origin)

- **Repo:** `qvest-digital/mxl-dmf-demo` (GitHub)
- **`origin/main`** ist ein veralteter Spiegel von `upstream/main` — aktuell **58 Commits hinter Upstream**
- **Keine eigenen Commits auf `main`** — alle Qvest-Erweiterungen leben auf Feature-Branches

### Branch-Übersicht

| Branch | Commits vs. main | Inhalt | Status |
|---|---|---|---|
| `cross-node-srt-mxl` | +50 | SRT-Sink, EFA-Dockerfiles, K8s-Manifeste, **+ 45 Upstream-Fabrics-Commits** | Primärer Integrationsbranch |
| `cross-node-mxl` | +50 | Identisch mit `cross-node-srt-mxl` | **Duplikat — löschen** |
| `fix/srt-sink-oom` | +53 | = `cross-node-srt-mxl` + OOM-Fix für SRT-Sink | Aktiver Bugfix |
| `fix/reuse-and-clang-format` | +52 | = `cross-node-srt-mxl` + SPDX/Formatting-Fixes | Aktiver Bugfix |
| `k8s-peyman` | +2 | K8s-Manifeste (Docker Desktop), SRT-Out-Beispiel | Basis für k8s-aws |
| `k8s-aws-peyman` | +6 | = `k8s-peyman` + AWS/EKS-Support, EFA-Manifeste | Produktions-Deployment |
| `msoent/multistage-build` | +7 | = `k8s-aws-peyman` + Multistage-Dockerfiles | Container-Builds |
| `dominik-main` | +1 | Install-Anleitung | Kann gemergt/gelöscht werden |
| `myk-local-mac` | +1 | Soft-RoCE Loopback-Test | Persönlicher Dev-Branch |

### Kernproblem

Der Branch `cross-node-srt-mxl` enthält **45 Upstream-Fabrics-Commits** (von Jonas Ohland/Riedel), die damals direkt in den Branch gezogen wurden, **bevor** sie in `upstream/main` gelandet sind. Diese Commits existieren jetzt mit **unterschiedlichen SHAs** in Upstream — ein einfacher Rebase/Merge ist daher nicht trivial.

### Qvest-spezifische Erweiterungen (Inventar)

**A. SRT-Streaming (`tools/mxl-gst/sink.cpp`)**
- GStreamer-Pipeline mit SRT-Output (mpegtsmux → leaky queue → srtsink)
- OOM-Fix: Leaky Queue (max 3 Buffer, 1s Latenz) verhindert Speicherüberlauf ohne Consumer
- x264enc Keyframe-Interval auf 30 Frames für schnelleren Consumer-Sync

**B. Containerisierung**
- Standard-Runtime-Images: `Dockerfile.reader.txt`, `Dockerfile.writer.txt`, etc.
- EFA-optimierte Images: `Dockerfile.reader.efa.txt`, `Dockerfile.writer.efa.txt`
- Multistage-Builds (Source-Build): `Dockerfile.reader.multistage`, etc.
- Compose-Files: `docker-compose.yaml`, `docker-compose.multistage.yaml`, `podman-compose.yaml`

**C. Kubernetes-Orchestrierung**
- Generische Manifeste: `kube-deployment-1-node.yaml`, `kube-deployment-3-nodes.yaml`, etc.
- AWS EKS-Manifeste: `kube-aws-1-node.yaml`, `kube-aws-2-nodes-efa.yaml`, etc.
- Infrastruktur-Skripte: `connect-kubectl.sh`, `install-eksctl.sh`, `cluster.yaml`

**D. Dokumentation**
- `examples/SRT_OUT_README.md`
- `examples/K8S_README.md`
- `examples/MULTISTAGE_README.md`
- `examples/aws/EFA_AWS_SETUP.md`, `examples/aws/K8S_AWS_SETUP.md`

---

## Soll-Zustand

```
upstream/main ──────────────────────────────────► origin/main (automatischer Spiegel)
                                                       │
                                                       │ automatischer Rebase
                                                       ▼
                                                  origin/qvest (alle Qvest-Patches)
                                                       │
                                              ┌────────┼────────┐
                                              ▼        ▼        ▼
                                          feature/   fix/    (kurzlebige
                                          ...        ...     Branches)
```

### Zielbild

1. **`main`** ist immer ein aktueller, automatisch synchronisierter Spiegel von `upstream/main`
2. **`qvest`** ist der Integrationsbranch mit allen Qvest-Erweiterungen, basierend auf `main`
3. Neue Features/Fixes branchen von `qvest` ab und werden per PR nach `qvest` gemergt
4. Docker-Images und Deployments werden aus `qvest` gebaut

---

## Umsetzung

### Phase 1: Main synchronisieren

**Ziel:** `origin/main` auf den aktuellen Stand von `upstream/main` bringen und automatisch halten.

#### Schritt 1.1: Einmaliger Sync

```bash
git fetch upstream
git checkout main
git merge --ff-only upstream/main
git push origin main
```

> Falls `--ff-only` fehlschlägt (sollte es nicht, da main keine eigenen Commits hat), 
> mit `git reset --hard upstream/main` und Force-Push arbeiten.

#### Schritt 1.2: Automatischer Sync per GitHub Actions

```yaml
# .github/workflows/sync-upstream.yml
name: Sync upstream → main

on:
  schedule:
    - cron: '0 6 * * 1'   # Jeden Montag um 06:00 UTC
  workflow_dispatch:        # Manuell auslösbar

permissions:
  contents: write

jobs:
  sync:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          ref: main
          fetch-depth: 0
          token: ${{ secrets.GITHUB_TOKEN }}

      - name: Add upstream remote
        run: |
          git remote add upstream https://github.com/dmf-mxl/mxl.git || true
          git fetch upstream main

      - name: Fast-forward main
        run: |
          git checkout main
          git merge --ff-only upstream/main

      - name: Push
        run: git push origin main

      - name: Notify on failure
        if: failure()
        run: |
          echo "::error::Upstream sync failed — main has diverged from upstream. Manual intervention required."
```

**Warum `--ff-only`:** Wenn der Merge fehlschlägt, hat jemand direkt auf `main` committet. Das ist ein bewusster Schutzmechanismus — `main` soll nie eigene Commits haben.

**Ergänzend:** Branch Protection Rule auf `main` setzen:
- Keine direkten Pushes (nur der Sync-Workflow)
- Verhindert versehentliche Commits auf main

---

### Phase 2: Qvest-Erweiterungen konsolidieren

**Ziel:** Alle Qvest-spezifischen Änderungen sauber auf einen neuen `qvest`-Branch bringen, basierend auf dem aktuellen `main`.

#### Schritt 2.1: Qvest-Commits identifizieren und cherry-picken

Die folgenden Commits enthalten die Qvest-Erweiterungen. Sie müssen in der richtigen Reihenfolge auf den neuen Branch übernommen werden.

**Aus `k8s-peyman` (Basis-K8s + SRT-Out):**
```
fb22d20  add: minimal working example for srt out + docu
61c8a45  k8s files for single/multiple nodes on docker-desktop + limit resources
```

**Aus `k8s-aws-peyman` (AWS-Erweiterung):**
```
528b7a2  add AWS support. testing in progress
9c822ff  setup mxl on AWS + EKS / single & double nodes
df61565  update aws nodes
9c9699c  EFA Yaml cross node
```

**Aus `cross-node-srt-mxl` (SRT-Sink + EFA-Images):**
```
2ef23c2  add sink, efa images, eks manifest files
```
> Hinweis: `23128c2 delete tfstate` nur übernehmen, falls die tfstate-Datei 
> durch einen vorherigen Commit eingeschleust wurde. Besser: per .gitignore ausschließen.

**Aus `msoent/multistage-build` (Multistage Docker):**
```
638ace3  feat: enabled multistage build + stream selection script
```

**Aus `fix/srt-sink-oom` (OOM-Bugfix):**
```
6c77fea  fix: add leaky queue before srtsink to prevent OOM when no consumer is connected
```

**Aus `fix/reuse-and-clang-format` (Code-Compliance):**
```
2837cd8  fix: add missing SPDX license headers to pass REUSE check
9a40d80  fix: remove trailing whitespace in sink.cpp to pass clang-format
```

**Aus `dominik-main` (Doku):**
```
84609dd  added install instructions
```

#### Schritt 2.2: Neuen Branch erstellen

```bash
# Sicherstellen, dass main aktuell ist
git checkout main
git pull origin main

# Neuen sauberen Branch erstellen
git checkout -b qvest

# Cherry-Pick in logischer Reihenfolge
# 1. SRT-Out Basis + Doku
git cherry-pick fb22d20

# 2. K8s-Manifeste
git cherry-pick 61c8a45

# 3. AWS-Support
git cherry-pick 528b7a2 9c822ff df61565 9c9699c

# 4. SRT-Sink + EFA-Images
git cherry-pick 2ef23c2

# 5. Multistage Docker Builds
git cherry-pick 638ace3

# 6. OOM-Bugfix
git cherry-pick 6c77fea

# 7. Code-Compliance (SPDX, Formatting)
git cherry-pick 2837cd8 9a40d80

# 8. Install-Anleitung
git cherry-pick 84609dd

# Push
git push -u origin qvest
```

> **Wichtig:** Bei Cherry-Pick-Konflikten manuell auflösen. Die meisten Konflikte 
> werden im Bereich `tools/mxl-gst/sink.cpp` auftreten, da sich Upstream dort 
> weiterentwickelt hat. Die Qvest-Änderungen (SRT-Pipeline, Leaky Queue) müssen 
> gegen den aktuellen Upstream-Stand angepasst werden.

#### Schritt 2.3: Aufräumen

Nach erfolgreicher Konsolidierung und Validierung:

```bash
# .gitignore ergänzen
echo "terraform.tfstate*" >> .gitignore
echo "examples/aws/kubeconfig" >> .gitignore
echo "examples/config" >> .gitignore

# Alte Branches löschen (nach Team-Absprache)
git push origin --delete cross-node-mxl          # Duplikat
git push origin --delete cross-node-srt-mxl      # Ersetzt durch qvest
git push origin --delete k8s-peyman              # Konsolidiert in qvest
git push origin --delete k8s-aws-peyman          # Konsolidiert in qvest
git push origin --delete msoent/multistage-build # Konsolidiert in qvest
git push origin --delete dominik-main            # Konsolidiert in qvest
git push origin --delete myk-local-mac           # Persönlicher Branch
git push origin --delete fix/srt-sink-oom        # Konsolidiert in qvest
git push origin --delete fix/reuse-and-clang-format  # Konsolidiert in qvest
```

---

### Phase 3: Qvest-Branch automatisch aktuell halten

**Ziel:** Nach jedem Upstream-Sync soll der `qvest`-Branch automatisch rebased werden.

#### Schritt 3.1: GitHub Actions Workflow

```yaml
# .github/workflows/rebase-qvest.yml
name: Rebase qvest branch on main

on:
  workflow_run:
    workflows: ["Sync upstream → main"]
    types: [completed]
  workflow_dispatch:

permissions:
  contents: write
  pull-requests: write
  issues: write

jobs:
  rebase:
    runs-on: ubuntu-latest
    if: ${{ github.event_name == 'workflow_dispatch' || github.event.workflow_run.conclusion == 'success' }}
    steps:
      - uses: actions/checkout@v4
        with:
          ref: qvest
          fetch-depth: 0
          token: ${{ secrets.GITHUB_TOKEN }}

      - name: Configure git
        run: |
          git config user.name "github-actions[bot]"
          git config user.email "github-actions[bot]@users.noreply.github.com"

      - name: Rebase qvest on main
        id: rebase
        run: |
          git fetch origin main
          git rebase origin/main
        continue-on-error: true

      - name: Push rebased branch
        if: steps.rebase.outcome == 'success'
        run: git push --force-with-lease origin qvest

      - name: Create issue on conflict
        if: steps.rebase.outcome == 'failure'
        run: |
          git rebase --abort
          gh issue create \
            --title "Rebase-Konflikt: qvest auf main" \
            --body "$(cat <<'ISSUE_EOF'
          ## Automatischer Rebase fehlgeschlagen

          Der Workflow \`rebase-qvest.yml\` konnte den Branch \`qvest\` nicht 
          automatisch auf \`main\` rebasen.

          ### Nächste Schritte

          1. Lokal rebasen und Konflikte auflösen:
             \`\`\`bash
             git fetch origin
             git checkout qvest
             git rebase origin/main
             # Konflikte auflösen...
             git rebase --continue
             git push --force-with-lease origin qvest
             \`\`\`

          2. Workflow erneut manuell auslösen (Actions → Rebase qvest → Run workflow)

          ISSUE_EOF
          )" \
            --label "automated,rebase-conflict"
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
```

#### Schritt 3.2: Ablauf im Normalfall

```
Montag 06:00 UTC
  │
  ├─ sync-upstream.yml: upstream/main → origin/main (ff-only)
  │   ✅ Erfolg
  │
  └─ rebase-qvest.yml (getriggert durch workflow_run)
      ├─ ✅ Kein Konflikt → automatischer Push (force-with-lease)
      └─ ❌ Konflikt → GitHub Issue wird erstellt
```

#### Schritt 3.3: Ablauf bei Konflikt

1. Ein Team-Mitglied bekommt das Issue zugewiesen
2. Lokaler Rebase mit manueller Konfliktauflösung
3. Push mit `--force-with-lease`
4. Issue schließen

---

## Zusammenfassung der Workflows

| Workflow | Trigger | Aktion | Bei Fehler |
|---|---|---|---|
| `sync-upstream.yml` | Montags 06:00 + manuell | `upstream/main` → `origin/main` (ff-only) | Error-Annotation, manuelle Intervention |
| `rebase-qvest.yml` | Nach erfolgreichem Sync + manuell | `qvest` auf `main` rebasen | GitHub Issue mit Anleitung |

---

## Regeln für die Zukunft

### Branch-Konventionen

| Branch | Zweck | Schutz |
|---|---|---|
| `main` | Upstream-Spiegel, nur automatische Updates | Protected: Keine direkten Pushes |
| `qvest` | Alle Qvest-Erweiterungen | Protected: PRs erforderlich, Force-Push nur durch Workflow |
| `feature/*` | Neue Qvest-Features, branchen von `qvest` | Kurzlebig, PR nach `qvest` |
| `fix/*` | Bugfixes, branchen von `qvest` | Kurzlebig, PR nach `qvest` |

### Commit-Workflow für neue Erweiterungen

```bash
git checkout qvest
git pull origin qvest
git checkout -b feature/mein-feature
# ... arbeiten ...
git push -u origin feature/mein-feature
# PR nach qvest erstellen
```

### Was NICHT auf main committet werden darf

- Keine Qvest-spezifischen Änderungen
- Keine Dockerfiles, K8s-Manifeste, Terraform-Dateien
- Keine Workflow-Änderungen (außer den Sync-Workflows selbst)

### Upstream-Contributions

Wenn ein Fix auch für Upstream relevant ist (z.B. der OOM-Fix in `sink.cpp`):
1. Separaten Branch von `main` erstellen
2. Fix dort isoliert implementieren
3. PR an `upstream` (dmf-mxl/mxl) erstellen
4. **Gleichzeitig** per Cherry-Pick in den `qvest`-Branch übernehmen
5. Sobald Upstream den Fix merged und er via Sync in `main` landet, wird der Cherry-Pick beim nächsten Rebase automatisch aufgelöst

---

## Risiken und Mitigierung

| Risiko | Wahrscheinlichkeit | Auswirkung | Mitigierung |
|---|---|---|---|
| Rebase-Konflikte bei Upstream-Änderungen an `sink.cpp` | Mittel | Manueller Aufwand | Regelmäßiger Sync (wöchentlich) reduziert Konfliktgröße |
| Vergessene Commits beim Cherry-Pick | Gering | Feature fehlt auf `qvest` | Checkliste in Phase 2, Review durch zweite Person |
| Jemand committet direkt auf `main` | Gering | Sync bricht | Branch Protection Rules |
| Upstream ändert Build-System grundlegend | Gering | Dockerfiles brechen | Rebase-Workflow erstellt Issue, manuell anpassen |
| Terraform State in Git-History | Bereits eingetreten | Sicherheitsrisiko | `.gitignore` + ggf. `git-filter-repo` für History-Bereinigung |

---

## Aufwandsschätzung

| Phase | Beschreibung | Aufwand |
|---|---|---|
| Phase 1 | Main synchronisieren + Workflow einrichten | 1–2h |
| Phase 2 | Cherry-Pick, Konflikte lösen, validieren, alte Branches löschen | 4–8h |
| Phase 3 | Rebase-Workflow einrichten + testen | 1–2h |
| **Gesamt** | | **6–12h** |

---

## Akzeptanzkriterien

- [ ] `origin/main` ist identisch mit `upstream/main`
- [ ] `sync-upstream.yml` läuft erfolgreich und ist auf wöchentlichen Cron gesetzt
- [ ] Branch `qvest` existiert, basiert auf aktuellem `main`, enthält alle Qvest-Erweiterungen
- [ ] `rebase-qvest.yml` läuft erfolgreich nach Upstream-Sync
- [ ] Alle alten Feature-Branches sind gelöscht (nach Team-Absprache)
- [ ] Branch Protection Rules sind für `main` und `qvest` konfiguriert
- [ ] `.gitignore` enthält Einträge für `terraform.tfstate*`, Kubeconfigs, etc.
- [ ] Docker-Build und K8s-Deployment funktionieren vom `qvest`-Branch aus
