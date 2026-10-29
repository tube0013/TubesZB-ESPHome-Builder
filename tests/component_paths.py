"""Locate the external repository for host tests; firmware sources always use GitHub."""
import os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REQUIRED=('network_settings','tubeszb_web','efr32_flasher','cc2652_flasher','zigbee_firmware_baud','cc2652_flash_guard')


def component_root():
    configured=os.environ.get('TZB_COMPONENTS_ROOT')
    candidates=[Path(configured)] if configured else [ROOT/'.ci/esphome-components/components']
    if not configured:
        cache=ROOT/'manifests/.esphome/external_components'
        if cache.is_dir():
            repos=list(cache.iterdir())
            repos.sort(key=lambda p:(p/'.git/FETCH_HEAD').stat().st_mtime if (p/'.git/FETCH_HEAD').exists() else p.stat().st_mtime,reverse=True)
            candidates.extend(p/'components' for p in repos if p.is_dir())
    for path in candidates:
        if all((path/name/'__init__.py').is_file() for name in REQUIRED):return path.resolve()
    raise RuntimeError('Set TZB_COMPONENTS_ROOT to an esphome-components checkout, or validate a manifest to populate its GitHub cache')


COMPONENTS_ROOT=component_root()
