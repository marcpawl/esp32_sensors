ESP32 project to read temperature sensors and post the results
to Google Sheets.


Development Environment
=======================


We use a devcontainer to stabilize the environment.

IDE: CLion 2026.2.3
-------------------

We use an Air Gapped configuration since our development
environment has slow internet and it causes CLion to 
fail to load the necessary backes,

To workaround this proble we use a Docker container serving
the JetBrains backends.

While connected to the internet exeute `python clion_remote_setup.py` to:
  -- Create the jetbrains-backends web server docker image
  -- Adjust /etc/localhosts to have an entry for jetbrains-backends
  -- Have the CLion configuration files point to jetbrains-backends
  -- Create the development image

