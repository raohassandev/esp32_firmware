# Automatrix PV-DG Controller — Product UX Architecture V2

## Product principle

The interface is an industrial operating product, not a collection of configuration forms. Every screen must answer one of three questions:

1. What is happening now?
2. Does anything require attention?
3. What is the safest next action?

Technical detail belongs in Engineering mode and must not compete with operator decisions.

## User scopes

### Operator

The operator monitors the plant, understands alarms, confirms current power state, and sees whether automatic control is available, blocked, or active.

Primary navigation:

- Overview
- Grid
- Solar
- Control
- Alarms

Secondary access:

- Controller information
- Display preferences
- Engineering sign-in

### Engineer / commissioner

The engineer configures communication, meters, inverter profiles, safety parameters, diagnostics, backup, and commissioning evidence.

Engineering navigation:

- Commissioning
- Network
- Meter setup
- Inverter setup
- Control parameters
- Diagnostics
- Controller service

## Global shell

### Header

The header contains only:

- Mobile navigation button when required
- Current page title and one-line context
- One system-health control
- One refresh action
- One overflow menu

Density, kiosk, theme, Engineering access, controller address, and secondary actions belong in the overflow menu or a dedicated page. They must not be separate permanent header buttons.

### Navigation

Desktop uses a stable left navigation rail. Mobile uses a five-item bottom operator navigation. Engineering tools never appear in the operator bottom navigation.

### Status presentation

The permanent six-column status strip is removed from normal pages. Overview contains the complete plant summary. Other pages receive only contextual status relevant to that page.

Global health is represented by one compact state:

- Normal
- Attention
- Critical
- Offline

Opening it reveals network, meter, solar, control, and alarm summaries.

## Page composition

Each page follows one structure:

1. Page context: title, purpose, current state
2. Primary decision area
3. Supporting evidence
4. Secondary actions
5. Engineering detail only when Engineering mode is active

No page should begin with multiple competing summaries or duplicate headings.

## Density and spacing

- Comfortable density is the default.
- Compact density is an optional display preference.
- Cards are grouped by decision purpose, not by data source.
- Touch targets are at least 44 px.
- Forms use one column on phones and a maximum of two columns on desktop unless fields are short and strongly related.
- Tables convert to cards or horizontally scroll with explicit affordance on narrow screens.

## Mobile behavior

- Header remains one row.
- Page title truncates only as a last resort; secondary text is hidden first.
- Bottom navigation contains Overview, Grid, Solar, Alarms, and Control.
- Refresh and system health remain accessible in the header.
- Display and Engineering actions move into the overflow sheet.
- Forms use sticky save actions only when data loss risk justifies them.

## Content rules

- Use operational language: Online, Waiting for data, Attention required, Control blocked.
- Avoid raw protocol language in operator mode.
- Never display register addresses, scale factors, function codes, or raw values to operators.
- Every warning includes a recommended action.
- Disabled features explain why they are unavailable.

## Release acceptance

The UX is not accepted until:

- header remains usable at 320 px;
- no horizontal page overflow exists at 320, 360, 390, 600, 1024, and 1366 px;
- operator and Engineering scopes are visibly distinct;
- every page has one dominant purpose;
- network commissioning can be completed from a phone;
- common operational questions are answered without opening Engineering mode;
- no feature adds an independent permanent header control.
