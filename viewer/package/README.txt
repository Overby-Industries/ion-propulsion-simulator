ABEP: Ion Propulsion Simulator - the orbit view and abep_run
Version {{VERSION}}
https://github.com/Overby-Industries/ion-propulsion-simulator

The question: how much electrical power would an air-breathing electric
thruster need to hold a satellite's altitude in low Earth orbit? abep_run.exe
simulates one mission (the orbit, the atmosphere, drag, thrust, solar power,
eclipses, the battery and the flight modes) and writes the results as CSV
files. The orbit view plays a run back as a picture.


TRY IT

1. Double-click ABEPOrbitView.exe. It plays an example run: one day of a
   100 kg satellite at 250 km. Drag to turn the view, use the mouse wheel to
   zoom, press Space to play or pause. "Follow" looks at the satellite from
   close by. The strip along the bottom shows the whole run; click it to
   jump to a time.

2. To run your own: open settings.txt in Notepad. Take the # off the front of
   a line, change the number after the = sign, and save. Then double-click
   "Simulate and view.bat". It runs abep_run.exe with your settings and
   opens the result in the viewer. The three result files, samples.csv,
   orbits.csv and summary.csv, are left in the folder my_run, to open in a
   spreadsheet or to read from Python. Each run replaces the last one.

   From a command prompt, abep_run.exe takes the same settings:
       abep_run.exe altitude_km=300 mass_kg=150 duration_days=3 out=my_run
   and "abep_run.exe --help" lists every setting. In the viewer, the Open...
   button plays any samples.csv.


WHAT TO KEEP IN MIND

- Thrust here is a parameter, not a prediction: power times an assumed thrust
  density, limited by an assumed intake capture. Real intake efficiency is
  unproven and, to our knowledge, ground tests have not yet shown net positive
  thrust. Read the results as sensitivities ("if the thrust density were X,
  the duty cycle would be Y"), not as evidence that a thruster will work.
- The defaults are illustrative, not data about any real satellite or
  thruster.
- One fixed atmosphere (the 1976 US Standard Atmosphere), gravity with J2 and
  nothing else, a fixed frontal area, no attitude dynamics. Real density
  varies strongly with solar activity, so decay times are only indicative.
- The flight modes (SAFE, CHARGE, COAST, THRUST, FAULT) follow thresholds that
  are operating policy, and the thruster's thermal numbers are assumptions.
- In the viewer the Earth is a stylised sphere and not real geography, and the
  satellite and the ribbons of the orbit are drawn larger than life.


WINDOWS MAY WARN

These programs are not signed with a certificate, so Windows SmartScreen or an
antivirus program may say that it does not know them. They are built from the
source code at the address above.


LICENCES

ABEP is under the Apache License 2.0 (LICENSE.txt). The orbit view is made with
the Godot Engine (MIT licence); licenses\Godot.txt has its notice and those of
the components inside it.
