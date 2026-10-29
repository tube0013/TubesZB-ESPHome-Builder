# Legacy static-IP builds during prerelease testing

The existing issue form, title gate, device choices, uploaded artifact link and
issue-comment action remain available. It builds the previous static-IP firmware,
not the new runtime-network-settings manifests.

The issue workflow `.github/workflows/main.yml` freezes three independent inputs:

- Builder source: `2026.10.25.0`, commit `7f968ca5aa95db46bd67e7be209946c45cb11bf5`.
- ESPHome: `2026.6.5`, verified in that release and the last successful issue build logs.
- Legacy components: `0e35b4afe1ad12b17418626d3bd75b7bcad52de6`, the component main baseline
  in place before that release; it is independent of component PR #7.

The form itself stays on the default branch. The workflow checks out the frozen
source by SHA, validates issue inputs, renders the old STATIC_IP/GATEWAY/SUBNET
placeholders and pins the old GitHub component imports before compiling. It keeps
the artifact finder that supports both native and PlatformIO output layouts.
Changes to current manifests, repository ESPHome version variables, or PR #7 do
not affect these issue builds. Old project-version labels in the frozen templates
remain unchanged. Nothing writes the stable or beta OTA feeds from the issue action.

The new firmware continues to import `refs/pull/7/head` and uses ESPHome 2026.10.1.
A draft prerelease creates no published firmware/feed changes. Publishing the
prerelease later updates Beta_Release and beta_ota_manifests only; stable
Latest_Release and ota_manifests remain on the existing release.

The legacy CI job validates every form device and compiles representative CC2652,
MGM24 and Z-Wave images with the frozen source and compiler. Retire or deliberately
advance these legacy pins only after the new firmware has completed testing.

The existing form choice `tubeszb-dual-radio-kit-mgm21-2022` is retained and mapped
to the released `tubeszb-dual-radio-mgm21-2022.yaml` file. The compile/artifact
steps use that canonical device name, fixing the old filename mismatch without
changing the form's options or using a new hardware configuration.
