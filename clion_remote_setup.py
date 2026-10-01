#!python

# Setup CLion to be able to work in an Air Gapped system.
# After exeucting this script you should be able to turn
# off your network and have CLion work in a devcontainer.

import argparse
import pathlib
import subprocess
import sys
import time

internal_site = 'http://jetbrains-backends:8080/'
productsInfoUrl = internal_site + 'backends/products.json'
clientDownloadUrl = internal_site + "clients/"
jreDownloadUrl = internal_site + "jre/"
pgpPublicKeyUrl = internal_site + "KEYS"


def add_to_hosts_if_missing(ip: str, hostname: str):
    """Appends an IP/Hostname mapping to /etc/hosts if the hostname is missing."""
    # -q silences output; -w ensures we match the exact hostname word
    check_command = ["grep", "-q", "-w", hostname, "/etc/hosts"]

    # Run the check (this does not require sudo just to read/grep the file)
    result = subprocess.run(check_command)

    # A return code of 1 means grep did NOT find the hostname
    if result.returncode == 1:
        entry = f"{ip} {hostname}\n"

        # Use sudo and tee to safely append to /etc/hosts
        append_command = ["sudo", "tee", "-a", "/etc/hosts"]

        # Pass the entry to stdin just like a pipe in bash
        subprocess.run(
            append_command, input=entry.encode(), stdout=subprocess.DEVNULL
        )
        print(f"Successfully added: {ip} -> {hostname}")
    else:
        print(f"Entry for hostname '{hostname}' already exists. Skipping.")

def check_web_server():
    """Check that the files are being served"""
    # Check that the files are being served
    urls = [ internal_site, 
            productsInfoUrl, clientDownloadUrl, jreDownloadUrl,
            pgpPublicKeyUrl ]
    for url in urls:
        subprocess.run(['curl', url], check=True)

def config_file_contents() -> dict[pathlib.Path, str]:
    """Return the expeted contents of the configuration files"""
    result = {}

    config_dir = pathlib.Path(".idea")

    productsInfoUrl_file = config_dir / "productsInfoUrl"
    result[productsInfoUrl_file] = productsInfoUrl

    clientDownloadUrl_file = config_dir / "clientDownloadUrl"
    result[clientDownloadUrl_file] = clientDownloadUrl

    jreDownloadUrl_file = config_dir / "jreDownloadUrl"
    result[jreDownloadUrl_file] = jreDownloadUrl

    pgpPublicKeyUrl_file = config_dir / "pgpPublicKeyUrl"
    result[pgpPublicKeyUrl_file] = pgpPublicKeyUrl

    return result

def clion_config_files():
    """Set up the remote configuration"""
    files = config_file_contents()
    for file_path in files:
        contents = files[file_path]
        with open(file_path, "w") as config:
            config.write(contents)
            config.write("\n")

def check_config_file(file_path: pathlib.Path):
    """Check that a configuration file is veing served."""
    with open(file_path, "r") as config:
        contents = config.readline()
        contents = contents.strip()
        subprocess.run(
                ['curl', '--output', '/dev/null', contents ],
                check=True)


def check_config_files():
    """Check that the contents of the configuration files is being served."""
    files = config_file_contents()
    for file_path in files:
        check_config_file(file_path)
    

def start_docker_container():
    """Start the docker container to serve the backends"""
    # Create the container for serving the backends
    cmds = [
      ['docker', 'compose', 'down', 'jetbrains-backends'],
      ['docker', 'compose', 'build', 'jetbrains-backends'],
      ['docker', 'compose', 'up', '-d', '--remove-orphans',
                    'jetbrains-backends']
    ]

    for cmd in cmds:
        print(cmd)
        subprocess.run(cmd, check=True, cwd="docker/jetbrains-backends")

def build_dev_container():
    cmd = ['docker', 'compose', 'build']
    print(cmd)
    subprocess.run(cmd, check=True)



def wait_for_container_running(
        container_name: str = "jetbrains-backends", 
        timeout: int = 60) -> bool:
    """Waits for a Docker container to reach a running state."""
    elapsed = 0
    poll_interval = 1
    
    print(f"Waiting for container '{container_name}' to start...")
    
    while elapsed < timeout:
        # Run docker inspect to fetch the Running state string ("true" or "false")
        result = subprocess.run(
            ["docker", "inspect", "-f", "{{.State.Running}}", container_name],
            capture_output=True,
            text=True,
            check=False
        )
        
        # If the command succeeds and returns 'true', the container is up
        if result.returncode == 0 and result.stdout.strip() == "true":
            print(f"Container '{container_name}' is up and running!")
            return True
            
        time.sleep(poll_interval)
        elapsed += poll_interval
        
    raise TimeoutError(f"Container '{container_name}' failed to start within {timeout} seconds.")

# Example usage:
# wait_for_container_running("my_app_container")


def main():
    parser = argparse.ArgumentParser(
        description="""
Configure environment to run CLion in a devcontainer, such
that the remote backends come from another container
""")

    args = parser.parse_args()

    add_to_hosts_if_missing("127.0.0.1", "jetbrains-backends")

    start_docker_container()
    wait_for_container_running()
    check_web_server()
    clion_config_files()
    check_config_files()
    build_dev_container()
    print("CLion is ready for remote development in Air Gapped system")
    print("")
    print("Disable the internet, and press return")
    sys.stdin.readline()
    print("")
    print("Open CLion, and reopen the project in the devcontainer.")
    print("Press return to continue.")
    sys.stdin.readline()
    print("")

    sys.exit(0)



if __name__ == "__main__":
    main()
