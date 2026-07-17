# ============================================================================
# QGroundPixel Custom Build Overrides
# Keep the full QGroundControl feature set (all flight stacks); only rebrand.
# ============================================================================

set(QGC_APP_NAME "QGroundPixel" CACHE STRING "App Name" FORCE)

# Full functionality retained: APM and PX4 plugin factories stay enabled, so
# none of the QGC_DISABLE_* flags from the example are set here.
