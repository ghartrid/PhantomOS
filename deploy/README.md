# PhantomOS VPS Deployment Guide

Deploy PhantomOS on a Namecheap Quasar VPS with browser access via your domain.

## Prerequisites

1. **Namecheap Quasar 1 VPS** (or higher) with Ubuntu 22.04/24.04
2. **Domain name** pointed to your VPS IP address
3. **SSH access** to your VPS

## Quick Start

### Step 1: Point Your Domain

In your domain registrar (Namecheap):
1. Go to Domain List → Manage → Advanced DNS
2. Add an A Record:
   - Host: `@` (or subdomain like `demo`)
   - Value: Your VPS IP address
   - TTL: Automatic

Wait 5-30 minutes for DNS propagation.

### Step 2: Upload PhantomOS to VPS

From your local machine:
```bash
# Create archive of PhantomOS
cd /home/graham/Desktop
tar -czvf phantomos.tar.gz phantomos/

# Upload to VPS
scp phantomos.tar.gz root@YOUR_VPS_IP:/root/
```

### Step 3: Run Setup on VPS

SSH into your VPS:
```bash
ssh root@YOUR_VPS_IP
```

Extract and run setup:
```bash
cd /root
tar -xzvf phantomos.tar.gz
cd phantomos/deploy
chmod +x vps-setup.sh
./vps-setup.sh
```

Follow the prompts:
- Enter your domain (e.g., `phantomos.yourdomain.com`)
- Enter your email (for SSL certificate)
- Set a VNC password

### Step 4: Access PhantomOS

Open your browser and go to:
```
https://yourdomain.com
```

Enter the VNC password when prompted. You'll see PhantomOS running!

## Managing PhantomOS

### Check Status
```bash
supervisorctl status
```

### Restart Services
```bash
supervisorctl restart phantomos-stack:*
```

### View Logs
```bash
# All logs
tail -f /var/log/phantomos/*.log

# Just PhantomOS output
tail -f /var/log/phantomos/phantomos.log
```

### Stop PhantomOS
```bash
supervisorctl stop phantomos-stack:*
```

### Start PhantomOS
```bash
supervisorctl start phantomos-stack:*
```

## Troubleshooting

### "Cannot connect" in browser
1. Check DNS: `dig yourdomain.com` should show your VPS IP
2. Check nginx: `systemctl status nginx`
3. Check firewall: `ufw status` (ports 80, 443 should be open)

### Black screen in noVNC
1. Check Xvfb: `supervisorctl status xvfb`
2. Check PhantomOS: `supervisorctl status phantomos`
3. View errors: `cat /var/log/phantomos/phantomos.err`

### PhantomOS crashes
```bash
# Check error log
cat /var/log/phantomos/phantomos.err

# Restart just PhantomOS
supervisorctl restart phantomos
```

### SSL certificate issues
```bash
# Renew certificate
certbot renew

# Check certificate status
certbot certificates
```

## Security Notes

- VNC is only accessible through the Nginx proxy (not directly exposed)
- All traffic is encrypted via HTTPS
- Change the VNC password for production use
- Consider adding HTTP basic auth for extra security:

```bash
# Add basic auth
apt install apache2-utils
htpasswd -c /etc/nginx/.htpasswd demouser

# Add to nginx config inside location / block:
#   auth_basic "PhantomOS Demo";
#   auth_basic_user_file /etc/nginx/.htpasswd;
```

## Architecture

```
[Browser] → HTTPS → [Nginx:443] → [noVNC/websockify:6080] → [x11vnc:5900] → [Xvfb:99] → [PhantomOS]
```

- **Xvfb**: Virtual framebuffer (fake display)
- **PhantomOS**: Runs on the virtual display
- **x11vnc**: Captures the virtual display
- **websockify/noVNC**: Converts VNC to WebSocket for browsers
- **Nginx**: Handles HTTPS and proxies to noVNC

## Updating PhantomOS

```bash
# Upload new binary
scp phantom-gui root@YOUR_VPS_IP:/opt/phantomos/

# Restart
ssh root@YOUR_VPS_IP "supervisorctl restart phantomos"
```
