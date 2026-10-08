---
name: Signature or device-type report
about: A device that was missed, mistyped, or wrongly triaged
labels: signatures
---

## What the device actually is

- Make and model:
- Type: <!-- camera / microphone / NVR / something else -->
- How it connects: <!-- Wi-Fi SoftAP, joined a network, BLE, several -->

## What Vulpecula said

From the INFO screen for that track, please include the whole evidence block —
the triage rule that fired is the first line, and that is usually the thing
that needs adjusting:

```
paste the INFO screen contents here
```

## From the CSV

The SD log holds the exact values, including the true case of names that the
display folds to upper case. The relevant columns:

```
band,ch,name,tier,peak,dtype,dtier,vendor,model,risk,why,evidence
```

## Why this matters

The signature tables carry a deliberate provenance warning: a wrong OUI puts a
confident vendor name and a triage label next to innocent hardware, which is
worse than no label. Field-confirmed reports are worth more than anything that
can be inferred from a registry, so please say how you confirmed the device is
what you say it is.
