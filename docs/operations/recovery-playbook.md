# Recovery playbook

1. **OTA failure**: inspect logs for explicit failure reason; device remains on current running partition.
2. **Bad provisioning input**: captive portal returns error/redirect, no config applied.
3. **Factory reset request**: invoke button or powercycle threshold; both run same reset pipeline.
4. **Hard recovery**: flash factory partition via serial and re-provision.
