# Seelie Installer Polish — Design Spec

**Date:** 2026-05-23
**Status:** approved

## Goal

Make the Windows installer (Inno Setup) visually cohesive with Seelie's Persona 5 brand identity instead of looking half-finished with one low-res image and Inno's default chrome.

## Context

The installer already has a `[Code]` section in `installer/inno/seelie.iss` that customizes welcome/finish page typography and sets Segoe UI as the font. It uses `WizardStyle=modern` with `WizardSizePercent=120`. The left sidebar image (`wizard-image.bmp`) is a high-quality 3D illustration. The top-right corner image (`wizard-small-image.bmp`) is low-resolution and blurry — the primary visual defect.

Seelie's brand palette: `#F36F1A` (Persona orange), `#1A1A1A` (near-black), `#888888` (secondary gray), white backgrounds.

## Sections

### 1. Regenerate wizard-small-image

Replace the blurry `wizard-small-image.bmp` with a sharp rendition of the Seelie character mascot.

**Source:** `assets/icons/seelie.png` (the anime-style blue-haired character art, 512×512)

**Outputs:**
- `installer/inno/branding/wizard-small-image.bmp` — 55×55 px
- `installer/inno/branding/wizard-small-image@2x.bmp` — 110×110 px

**Process:** Use ImageMagick (already in the build toolchain per `.gitignore` comments). Center the character art on a soft blue/cyan gradient matching the existing wizard-image background tone. Save as 24-bit BMP with alpha (Inno 6 supports `WizardImageAlphaFormat=defined`).

**Command (reference — IM7 syntax; IM6 drops `magick` prefix):**
```bash
magick convert assets/icons/seelie.png \
  -resize 96x96 -gravity center \
  -background 'gradient:#D0E8FF-#A0D0F0' \
  -extent 110x110 \
  installer/inno/branding/wizard-small-image@2x.bmp
magick convert installer/inno/branding/wizard-small-image@2x.bmp \
  -resize 55x55 \
  installer/inno/branding/wizard-small-image.bmp
```

### 2. Custom page descriptions

Override Inno's default page subtitles in `CurPageChanged()` to give every page branded, conversational text.

| Page | PageNameLabel (bold header) | PageDescriptionLabel (subtitle) |
|------|----------------------------|--------------------------------|
| SelectDir | "Where should Seelie live?" | "Choose a folder or accept the default." |
| SelectTasks | "Almost ready" | "Pick any extras you want." |
| Ready | "Ready to install" | "Everything looks good. Click Install to begin." |
| Installing | "Installing Seelie..." | (dynamically updated by Inno) |

**Code (add inside `CurPageChanged`):**
```pascal
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
end;
```

Style the labels to match the Persona 5 typography (Segoe UI, near-black for header, secondary gray for subtitle) — apply in `InitializeWizard`.

### 3. Branded progress bar

Replace the system-green progress bar with Seelie orange (`#F36F1A`).

**Mechanism:** Send `PBM_SETBARCOLOR` (0x0409) to `WizardForm.ProgressGauge.Handle` during `CurPageChanged(wpInstalling)`.

**Code:**
```pascal
const
  PBM_SETBARCOLOR = $0409;
  SEELIE_ORANGE   = $001A6FF3;  // BGR (Windows COLORREF): #F36F1A → 0xF3,0x6F,0x1A → bytes reversed

// In CurPageChanged:
wpInstalling:
begin
  SendMessage(WizardForm.ProgressGauge.Handle, PBM_SETBARCOLOR, 0, SEELIE_ORANGE);
end;
```

### 4. Typography and chrome refinements

Three small changes in `InitializeWizard`:

1. **Remove the 3D bevel border** around page panels:
   ```pascal
   WizardForm.Bevel.Visible := False;
   ```

2. **Set main panel background to white** (some Inno themes default to clBtnFace):
   ```pascal
   WizardForm.MainPanel.Color := clWhite;
   ```

3. **Bold both navigation buttons**, not just Next:
   ```pascal
   WizardForm.NextButton.Font.Style := [fsBold];
   WizardForm.BackButton.Font.Style := [fsBold];
   ```

## Files Changed

| File | Change |
|------|--------|
| `installer/inno/seelie.iss` | Add Sections 2–4 Pascal code to `[Code]` |
| `installer/inno/branding/wizard-small-image.bmp` | Replace with sharp version |
| `installer/inno/branding/wizard-small-image@2x.bmp` | Replace with sharp 2x version |

`wizard-image.bmp` stays unchanged — it's already polished.

## Constraints

- Inno Setup 6.x minimum (already required by `build_inno.py`)
- ImageMagick must be available (already used for current branding per `.gitignore`)
- No runtime dependencies change — all wizard pages work offline
- Uninstaller UI is not customized (Inno's uninstall wizard has no branding hooks)

## Out of Scope

- Custom checkboxes/radio buttons (Inno uses native Windows controls; restyling would require owner-draw)
- Replacing `wizard-image.bmp` (already high quality)
- Language-picker page styling (Inno's built-in dialog, not scriptable)
- macOS DMG / Linux AppImage installer design
