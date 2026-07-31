# Evidence the operator can act on

## Why this file exists

Recorded from the product owner, 2026-07-31:

> *"Pakistan men choti factories k log deep chezon ko nhi smjhte, sometime
> illogical baten krty hen. tu kam k doran khayal rakhen k koi aesa variable,
> value jo fault tracing, trouble shooting ki assan kr skti he ya user ko
> controller k theek hone ka proof dy skti he wo bhi hm ne soch smjh kr frontend
> pe le k ani he."*

Two different needs, and they are not the same job.

**Fault tracing.** When something is wrong, the screen should say *which* thing
and *why*, in terms of the plant rather than of the firmware. "Inverter 2 has not
answered for 4 minutes" sends an electrician to a cable. "ESP_ERR_TIMEOUT" sends
them to us.

**Proof of correctness.** When nothing is wrong, the screen should still be able
to prove it. A controller that shows nothing when it is working looks identical
to a controller that has crashed, and the customer who cannot tell the difference
will conclude the product is broken and say so. This is the harder of the two and
the one that gets forgotten, because it has no bug report behind it.

The firmware already computes almost everything needed for both. The gap is
usually that a value exists in C and never reaches a screen.

## The markers

Two greppable markers, used in source comments where the value is produced:

| Marker | Meaning |
|---|---|
| `HMI-EVIDENCE:` | This value would shorten a fault trace or prove the controller is working, and it is not on a screen yet. |
| `PHASE-2:` | Deliberately deferred, with the reason stated. |

Find them with:

```
grep -rn "HMI-EVIDENCE:\|PHASE-2:" components/ web/ main/
```

A marker is a note, not a promise, and it carries no schedule. Its whole purpose
is that the thought is not lost between the moment the value is computed and the
moment somebody designs the screen.

## What makes a good candidate

Ask whether it answers one of these, in plant terms:

- **Is it alive?** Last successful read, in seconds, per device. Not a boolean --
  "online" tells an operator nothing about a link that works one poll in three.
- **Why is it not doing what I expect?** The controller already produces one
  sentence per inhibit reason and per commissioning blocker. Those are the single
  highest-value strings in the product and they belong where the operator looks
  first, not behind an engineering login.
- **Did the command land?** Requested against confirmed, and *what the
  confirmation rests on*. A number that was accepted and a number that was
  demonstrated are different claims and the product already distinguishes them.
- **Is it being limited, and by what?** PV held down by the generator floor,
  by the grid policy, or by a ramp are three different situations with three
  different remedies, and they look identical from outside.
- **Has anything changed underneath me?** A pending restart, a configuration
  saved but not in force, a source changeover. Each has produced a support call
  in this project already.

## What is not a candidate

- Raw error enums and register addresses, on an operator screen. They belong in
  the engineering view, where somebody can act on them.
- Anything that would need a Modbus transaction from an HTTP handler to answer.
  The acquisition path exists precisely so that screens read cached state.
- A value with no defined meaning when it is absent. "0 kW" and "not measured"
  must never render the same way; this product has already had that defect three
  separate times.
