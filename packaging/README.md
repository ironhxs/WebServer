# WebServer Debian Package Instructions

## Prerequisites

```bash
sudo apt-get install devscripts build-essential debhelper \
    libmysqlclient-dev mysql-server
```

## Building the Package

### 1. From project root:

```bash
# Clean previous builds
make distclean

# Build Debian packages
cd packaging
dpkg-buildpackage -us -uc -b

# Packages will be created in parent directory:
# - webserver_1.0.0-1_amd64.deb
# - webbench_1.0.0-1_amd64.deb
```

### 2. Alternative: Using debuild

```bash
cd packaging
debuild -us -uc -b
```

## Installing

```bash
# Install webserver (includes dependencies)
sudo dpkg -i ../webserver_1.0.0-1_amd64.deb
sudo apt-get install -f  # Fix dependencies if needed

# Install webbench (optional)
sudo dpkg -i ../webbench_1.0.0-1_amd64.deb
```

## Post-Installation

```bash
# 1. Initialize database
mysql -u root -p < /usr/share/doc/webserver/setup_db.sql

# 2. Configure server (optional)
sudo cp /etc/webserver/server.conf.example /etc/webserver/server.conf
sudo nano /etc/webserver/server.conf

# 3. Start server
webserver -p 9006

# Or use systemd (if service file is created)
sudo systemctl start webserver
sudo systemctl enable webserver
```

## Package Contents

### webserver package:
- `/usr/bin/webserver` - Main executable
- `/etc/webserver/server.conf.example` - Configuration template
- `/var/www/webserver/` - Web resources
- `/usr/share/doc/webserver/` - Documentation
- `/usr/share/man/man1/webserver.1.gz` - Man page

### webbench package:
- `/usr/bin/webbench` - Benchmark tool
- `/usr/share/man/man1/webbench.1.gz` - Man page

## Uninstalling

```bash
sudo dpkg -r webserver webbench
# or
sudo apt-get remove webserver webbench
```

## Building for Distribution

```bash
# Build source package
dpkg-buildpackage -S

# Sign packages
debsign ../webserver_1.0.0-1_amd64.changes

# Upload to repository (if you have one)
dput ppa:your-ppa ../webserver_1.0.0-1_source.changes
```

## Testing Package

```bash
# Check package quality
lintian ../webserver_1.0.0-1_amd64.deb
lintian ../webbench_1.0.0-1_amd64.deb

# List package contents
dpkg -c ../webserver_1.0.0-1_amd64.deb

# Show package info
dpkg -I ../webserver_1.0.0-1_amd64.deb
```

## Troubleshooting

### Build fails with missing dependencies:
```bash
sudo mk-build-deps -i packaging/debian/control
```

### Permission errors:
```bash
chmod +x packaging/debian/rules
```

### Man page errors:
```bash
# Verify man page format
man -l docs/manuals/webserver.1
groff -man -Tascii docs/manuals/webserver.1
```
