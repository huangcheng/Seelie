# Seelie Installer Polish — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Windows installer visually cohesive with Seelie's Persona 5 brand by replacing the blurry small image, adding custom page text, branding the progress bar, and polishing typography.

**Architecture:** Modify the existing Inno Setup script (`seelie.iss`) by extending the `[Code]` section with Pascal event handlers that style every wizard page. Generate new branding bitmaps from the existing `seelie.png` mascot art. Test by compiling the installer with `build_inno.py`.

**Tech Stack:** Inno Setup 6.x, ImageMagick, Python 3 (build script)

---

### Task 1: Generate sharp wizard-small-image bitmaps

**Files:**
- Create: `installer/inno/branding/wizard-small-image.bmp`
- Create: `installer/inno/branding/wizard-small-image@2x.bmp`

- [ ] **Step 1: Check ImageMagick is available**

Run:
```bash
magick --version
```
Expected: ImageMagick 7.x version output.

If `magick` is not found, try `convert --version` for ImageMagick 6.x.

- [ ] **Step 2: Generate the 2x version (110×110)**

Run:
```bash
cd F:\Seelie
magick convert assets/icons/seelie.png ^
  -resize 96x96 -gravity center ^
  -background "gradient:#D0E8FF-#A0D0F0" ^
  -extent 110x110 ^
  installer/inno/branding/wizard-small-image@2x.bmp
```

If on IM6, remove `magick` prefix:
```bash
convert assets/icons/seelie.png ^
  -resize 96x96 -gravity center ^
  -background "gradient:#D0E8FF-#A0D0F0" ^
  -extent 110x110 ^
  installer/inno/branding/wizard-small-image@2x.bmp
```

- [ ] **Step 3: Generate the 1x version (55×55)**

Run:
```bash
magick convert installer/inno/branding/wizard-small-image@2x.bmp ^
  -resize 55x55 ^
  installer/inno/branding/wizard-small-image.bmp
```

- [ ] **Step 4: Verify files exist**

Run:
```bash
dir installer\inno\branding\wizard-small-image*.bmp
```

Expected: Both `wizard-small-image.bmp` and `wizard-small-image@2x.bmp` listed.

- [ ] **Step 5: Commit**

```bash
git add installer/inno/branding/wizard-small-image.bmp installer/inno/branding/wizard-small-image@2x.bmp
git commit -m "installer: regenerate wizard-small-image at sharp resolution"
```

---

### Task 2: Add custom page descriptions

**Files:**
- Modify: `installer/inno/seelie.iss` — extend `[Code]` section

- [ ] **Step 1: Style the page name/description labels in InitializeWizard**

In `seelie.iss`, find the `InitializeWizard()` procedure (around line 89). Add the label styling immediately after the existing font setup:

```pascal
procedure InitializeWizard();
begin
  { Increase wizard size for a more spacious, modern feel }
  WizardForm.ClientWidth := ScaleX(600);
  WizardForm.ClientHeight := ScaleY(420);

  { White background, clean typography }
  WizardForm.Color := clWhite;
  WizardForm.Font.Name := 'Segoe UI';
  WizardForm.Font.Size := 9;
  WizardForm.Font.Color := NEAR_BLACK;

  { --- Page name + description label styling (all pages) --- }
  if WizardForm.PageNameLabel <> nil then
  begin
    WizardForm.PageNameLabel.Font.Name := 'Segoe UI';
    WizardForm.PageNameLabel.Font.Size := 16;
    WizardForm.PageNameLabel.Font.Style := [fsBold];
    WizardForm.PageNameLabel.Font.Color := NEAR_BLACK;
  end;
  if WizardForm.PageDescriptionLabel <> nil then
  begin
    WizardForm.PageDescriptionLabel.Font.Name := 'Segoe UI';
    WizardForm.PageDescriptionLabel.Font.Size := 9;
    WizardForm.PageDescriptionLabel.Font.Color := GRAY_SECONDARY;
  end;

  { --- Welcome Page styling --- }
  if WizardForm.WelcomeLabel1 <> nil then
  ...
```

- [ ] **Step 2: Add per-page caption overrides in CurPageChanged**

Find the existing `CurPageChanged` procedure (around line 149). Replace it with:

```pascal
procedure CurPageChanged(CurPageID: Integer);
begin
  WizardForm.NextButton.Font.Style := [fsBold];
  WizardForm.BackButton.Font.Style := [fsBold];

  case CurPageID of
    wpSelectDir:
    begin
      WizardForm.PageNameLabel.Caption := 'Where should Seelie live?';
      WizardForm.PageDescriptionLabel.Caption := 'Choose a folder or accept the default.';
    end;
    wpSelectTasks:
    begin
      WizardForm.PageNameLabel.Caption := 'Almost ready';
      WizardForm.PageDescriptionLabel.Caption := 'Pick any extras you want.';
    end;
    wpReady:
    begin
      WizardForm.PageNameLabel.Caption := 'Ready to install';
      WizardForm.PageDescriptionLabel.Caption := 'Everything looks good. Click Install to begin.';
    end;
    wpInstalling:
    begin
      WizardForm.PageNameLabel.Caption := 'Installing Seelie...';
      WizardForm.PageDescriptionLabel.Caption := '';
    end;
  end;
end;
```

- [ ] **Step 3: Commit**

```bash
git add installer/inno/seelie.iss
git commit -m "installer: custom branded text for every wizard page"
```

---

### Task 3: Add branded progress bar

**Files:**
- Modify: `installer/inno/seelie.iss` — extend `[Code]` constants + `CurPageChanged`

- [ ] **Step 1: Add progress bar color constant**

In the `[Code]` section, add the constant after the existing color constants (around line 86):

```pascal
const
  NEAR_BLACK     = $001A1A1A;
  GRAY_SECONDARY = $00888888;
  SEELIE_ORANGE  = $001A6FF3;  // BGR (Windows COLORREF): #F36F1A
  PBM_SETBARCOLOR = $0409;
```

- [ ] **Step 2: Set progress bar color on Installing page**

In the `CurPageChanged` case block (already modified in Task 2), add the `SendMessage` call inside the `wpInstalling` case:

```pascal
    wpInstalling:
    begin
      WizardForm.PageNameLabel.Caption := 'Installing Seelie...';
      WizardForm.PageDescriptionLabel.Caption := '';
      SendMessage(WizardForm.ProgressGauge.Handle, PBM_SETBARCOLOR, 0, SEELIE_ORANGE);
    end;
```

- [ ] **Step 3: Commit**

```bash
git add installer/inno/seelie.iss
git commit -m "installer: brand progress bar with Seelie orange (#F36F1A)"
```

---

### Task 4: Typography and chrome refinements

**Files:**
- Modify: `installer/inno/seelie.iss` — extend `InitializeWizard`

- [ ] **Step 1: Remove 3D bevel and set panel background**

In `InitializeWizard()`, add after the font setup (around line 98, right after `WizardForm.Font.Color := NEAR_BLACK;`):

```pascal
  { Remove dated 3D bevel border; ensure white panel background }
  WizardForm.Bevel.Visible := False;
  WizardForm.MainPanel.Color := clWhite;
```

- [ ] **Step 2: Commit**

```bash
git add installer/inno/seelie.iss
git commit -m "installer: remove 3D bevel, set white panel background"
```

---

### Task 5: Build and verify

**Files:**
- Test: `installer/inno/seelie.iss` compiles cleanly
- Verify: `build/SeelieSetup-<version>.exe` is generated

- [ ] **Step 1: Ensure staging dir is populated**

If `build/staging/Seelie.exe` does not exist, run the full staging first:
```bash
cd F:\Seelie
python scripts/build_release.py
```
If the staging already exists, you can skip this and use `--skip-stage`.

- [ ] **Step 2: Compile the installer**

```bash
python scripts/build_inno.py --skip-stage
```

Expected output ends with:
```
[SUCCESS] build/SeelieSetup-X.Y.Z.exe  (XX.X MB)
```

If it fails, ISCC.exe will print Pascal compilation errors — fix them and retry.

- [ ] **Step 3: Visual inspection checklist**

Launch `build/SeelieSetup-X.Y.Z.exe` and verify:

1. Welcome page: Seelie character art is crisp in the top-right corner (no pixelation)
2. SelectDir page: Header reads "Where should Seelie live?", subtitle reads "Choose a folder or accept the default."
3. SelectTasks page: Header reads "Almost ready", subtitle reads "Pick any extras you want."
4. Ready page: Header reads "Ready to install", subtitle reads "Everything looks good. Click Install to begin."
5. Installing page: Header reads "Installing Seelie...", progress bar is orange (`#F36F1A`), not green
6. Finish page: Has no 3D bevel border around the page content
7. All text uses Segoe UI (not the Windows default MS Shell Dlg)

- [ ] **Step 4: Commit final state**

```bash
git status  # should show only tracked files changed
git log --oneline -5
```

No further commit needed — the installer build artifacts are in `build/` which is gitignored.

---

## Spec Coverage Check

| Spec Section | Task |
|---|---|
| 1. Regenerate wizard-small-image | Task 1 |
| 2. Custom page descriptions | Task 2 |
| 3. Branded progress bar | Task 3 |
| 4. Typography and chrome refinements | Task 4 |
| Build + verify | Task 5 |

All sections covered. No placeholders.
