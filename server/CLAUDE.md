# Smart Fridge Server - VPS Setup Instructions

## Overview

This is the backend server for the Smart Fridge Barcode Scanner project.
It receives barcode scans from ESP32 devices, stores product data, performs OCR for expiry dates, and provides a web interface for fridge inventory management.

**Domain:** frigo.xamad.net
**Stack:** Node.js + Express + SQLite + Tesseract.js

## VPS Requirements

- Ubuntu 22.04+ or Debian 12+
- Node.js 18+ (LTS recommended)
- Nginx (reverse proxy)
- Certbot (SSL via Let's Encrypt)
- 1GB RAM minimum
- 10GB disk space

## Installation Steps

### 1. System Update & Node.js Installation

```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install Node.js 20 LTS
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs

# Verify
node --version
npm --version
```

### 2. Install Build Tools (for better-sqlite3)

```bash
sudo apt install -y build-essential python3 git
```

### 3. Create Application Directory

```bash
sudo mkdir -p /var/www/smartfridge
sudo chown $USER:$USER /var/www/smartfridge
cd /var/www/smartfridge
```

### 4. Copy Server Files

Copy all files from this `server/` folder to `/var/www/smartfridge/`:
- server.js
- package.json
- public/index.html

```bash
# Or clone from GitHub
git clone https://github.com/xamad/SmartFridgeScanner.git
cp -r SmartFridgeScanner/server/* /var/www/smartfridge/
```

### 5. Install Dependencies

```bash
cd /var/www/smartfridge
npm install
```

### 6. Create Upload Directory

```bash
mkdir -p uploads/products
chmod 755 uploads uploads/products
```

### 7. Test Run

```bash
node server.js
# Should show: "Smart Fridge Server on port 3000"
# Ctrl+C to stop
```

### 8. Setup PM2 Process Manager

```bash
sudo npm install -g pm2

cd /var/www/smartfridge
pm2 start server.js --name smartfridge
pm2 save
pm2 startup
# Follow the command it outputs
```

### 9. Configure Nginx Reverse Proxy

```bash
sudo nano /etc/nginx/sites-available/smartfridge
```

Paste this configuration:

```nginx
server {
    listen 80;
    server_name frigo.xamad.net;

    client_max_body_size 10M;

    location / {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_cache_bypass $http_upgrade;
    }

    location /images/ {
        alias /var/www/smartfridge/uploads/products/;
        expires 7d;
        add_header Cache-Control "public, immutable";
    }
}
```

Enable the site:

```bash
sudo ln -s /etc/nginx/sites-available/smartfridge /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx
```

### 10. Setup SSL with Certbot

```bash
sudo apt install -y certbot python3-certbot-nginx
sudo certbot --nginx -d frigo.xamad.net
```

### 11. Configure Firewall

```bash
sudo ufw allow 80
sudo ufw allow 443
sudo ufw enable
```

## API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | /api/product | Receive barcode scan from ESP32 |
| POST | /api/ocr | OCR image for expiry date extraction |
| GET | /api/inventory | List all products in fridge |
| GET | /api/expiring?days=7 | Products expiring soon |
| GET | /api/shopping | Get shopping list |
| POST | /api/shopping | Add to shopping list |
| DELETE | /api/shopping/:id | Remove from shopping list |
| POST | /api/manual | Manual product entry |
| PUT | /api/product/:id | Update product |
| DELETE | /api/product/:id | Delete product |
| GET | /api/stats | Statistics |

## API Request Examples

### ESP32 sends barcode (with image):
```bash
curl -X POST https://frigo.xamad.net/api/product \
  -F "action=add" \
  -F "barcode=8001234567890" \
  -F "expiry_date=2026-03-15" \
  -F "device=ESP32-CAM" \
  -F "image=@photo.jpg"
```

### ESP32 sends barcode (JSON only):
```bash
curl -X POST https://frigo.xamad.net/api/product \
  -H "Content-Type: application/json" \
  -d '{"action":"add","barcode":"8001234567890","expiry_date":"2026-03-15","device":"ESP32-S3"}'
```

### OCR Request:
```bash
curl -X POST https://frigo.xamad.net/api/ocr \
  -F "image=@product_label.jpg"
```

## Maintenance Commands

```bash
# View logs
pm2 logs smartfridge

# Restart server
pm2 restart smartfridge

# Check status
pm2 status

# Update code
cd /var/www/smartfridge
git pull
npm install
pm2 restart smartfridge
```

## Backup Database

```bash
# Manual backup
cp /var/www/smartfridge/fridge.db ~/backup_fridge_$(date +%Y%m%d).db

# Setup daily cron backup
crontab -e
# Add: 0 3 * * * cp /var/www/smartfridge/fridge.db /root/backups/fridge_$(date +\%Y\%m\%d).db
```

## Troubleshooting

### Server won't start
```bash
pm2 logs smartfridge --lines 50
# Check for errors
```

### Nginx 502 Bad Gateway
```bash
# Make sure Node.js server is running
pm2 status
pm2 restart smartfridge
```

### SSL Certificate renewal
```bash
sudo certbot renew --dry-run
# Certbot auto-renews, but you can force:
sudo certbot renew
```

### Database locked
```bash
pm2 restart smartfridge
```

## Security Notes

- The server uses `client.setInsecure()` in firmware - consider adding API key authentication
- Upload directory should not be executable
- Consider rate limiting for production use
- Database is stored in `/var/www/smartfridge/fridge.db` - backup regularly

## Future Enhancements (TODO)

- [ ] Add API key authentication
- [ ] Telegram bot for notifications
- [ ] Email alerts for expiring products
- [ ] Barcode lookup API integration (Open Food Facts)
- [ ] Multi-user support
- [ ] Mobile app (React Native)
