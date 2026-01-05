const express = require('express');
const cors = require('cors');
const multer = require('multer');
const Database = require('better-sqlite3');
const Tesseract = require('tesseract.js');
const cron = require('node-cron');
const path = require('path');
const fs = require('fs');

const app = express();
const PORT = process.env.PORT || 3000;

app.use(cors());
app.use(express.json());
app.use(express.static('public'));
app.use('/images', express.static('uploads/products'));

const storage = multer.diskStorage({
    destination: (req, file, cb) => {
        const dir = 'uploads/products';
        if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
        cb(null, dir);
    },
    filename: (req, file, cb) => cb(null, Date.now() + '-' + file.originalname)
});
const upload = multer({ storage, limits: { fileSize: 5 * 1024 * 1024 } });

const db = new Database('fridge.db');
db.exec(`
    CREATE TABLE IF NOT EXISTS products (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        barcode TEXT NOT NULL,
        name TEXT,
        brand TEXT,
        category TEXT,
        quantity INTEGER DEFAULT 1,
        expiry_date TEXT,
        image_path TEXT,
        added_date TEXT DEFAULT CURRENT_TIMESTAMP,
        finished INTEGER DEFAULT 0
    );
    CREATE TABLE IF NOT EXISTS shopping_list (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        barcode TEXT NOT NULL,
        name TEXT,
        quantity_needed INTEGER DEFAULT 1,
        priority TEXT DEFAULT 'normal',
        auto_generated INTEGER DEFAULT 0,
        added_date TEXT DEFAULT CURRENT_TIMESTAMP,
        purchased INTEGER DEFAULT 0
    );
    CREATE TABLE IF NOT EXISTS scan_history (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        barcode TEXT NOT NULL,
        action TEXT NOT NULL,
        device TEXT,
        image_path TEXT,
        timestamp TEXT DEFAULT CURRENT_TIMESTAMP
    );
`);
console.log('Database initialized');

// POST /api/product - Receive from ESP32
app.post('/api/product', upload.single('image'), (req, res) => {
    const { action, barcode, expiry_date, device } = req.body;
    const imagePath = req.file ? '/images/' + req.file.filename : null;
    console.log('[PRODUCT] ' + action + ': ' + barcode);

    db.prepare('INSERT INTO scan_history (barcode, action, device, image_path) VALUES (?, ?, ?, ?)').run(barcode, action, device, imagePath);

    if (action === 'add') {
        const existing = db.prepare('SELECT * FROM products WHERE barcode = ? AND finished = 0').get(barcode);
        if (existing) {
            db.prepare('UPDATE products SET quantity = quantity + 1, image_path = COALESCE(?, image_path) WHERE id = ?').run(imagePath, existing.id);
        } else {
            db.prepare('INSERT INTO products (barcode, expiry_date, image_path) VALUES (?, ?, ?)').run(barcode, expiry_date || null, imagePath);
        }
        db.prepare('DELETE FROM shopping_list WHERE barcode = ?').run(barcode);
        res.json({ success: true, message: 'Prodotto aggiunto' });
    } else if (action === 'remove') {
        const existing = db.prepare('SELECT * FROM products WHERE barcode = ? AND finished = 0').get(barcode);
        if (existing) {
            if (existing.quantity > 1) {
                db.prepare('UPDATE products SET quantity = quantity - 1 WHERE id = ?').run(existing.id);
            } else {
                db.prepare('UPDATE products SET finished = 1, quantity = 0 WHERE id = ?').run(existing.id);
                if (existing.image_path) {
                    const imgFile = 'uploads/products/' + path.basename(existing.image_path);
                    if (fs.existsSync(imgFile)) fs.unlinkSync(imgFile);
                }
                if (!db.prepare('SELECT * FROM shopping_list WHERE barcode = ?').get(barcode)) {
                    db.prepare('INSERT INTO shopping_list (barcode, name, auto_generated) VALUES (?, ?, 1)').run(barcode, existing.name || barcode);
                }
            }
        }
        res.json({ success: true, message: 'Prodotto rimosso' });
    } else {
        res.status(400).json({ success: false });
    }
});

// POST /api/ocr - OCR for expiry date
app.post('/api/ocr', upload.single('image'), async (req, res) => {
    if (!req.file) return res.status(400).json({ error: 'No image' });
    try {
        const { data: { text } } = await Tesseract.recognize(req.file.path, 'ita+eng');
        const datePatterns = [/(\d{2})[\/\-\.](\d{2})[\/\-\.](\d{4})/g, /(\d{2})[\/\-\.](\d{2})[\/\-\.](\d{2})/g];
        let expiry_date = null;
        for (const p of datePatterns) { const m = text.match(p); if (m) { expiry_date = m[0]; break; } }
        res.json({ expiry_date, confidence: expiry_date ? 0.7 : 0 });
    } catch (e) { res.status(500).json({ error: 'OCR failed' }); }
});

// GET /api/inventory
app.get('/api/inventory', (req, res) => {
    const products = db.prepare('SELECT * FROM products WHERE finished = 0 ORDER BY expiry_date ASC').all();
    res.json({ products, count: products.length });
});

// GET /api/expiring
app.get('/api/expiring', (req, res) => {
    const days = parseInt(req.query.days) || 7;
    const sql = `SELECT * FROM products WHERE finished = 0 AND expiry_date IS NOT NULL AND date(expiry_date) <= date('now', '+${days} days') ORDER BY expiry_date ASC`;
    const products = db.prepare(sql).all();
    res.json({ products, count: products.length, days });
});

// GET /api/shopping
app.get('/api/shopping', (req, res) => {
    const items = db.prepare('SELECT * FROM shopping_list WHERE purchased = 0 ORDER BY added_date DESC').all();
    res.json({ shopping_list: items, total_items: items.length });
});

// POST /api/shopping
app.post('/api/shopping', (req, res) => {
    const { barcode, name, quantity } = req.body;
    db.prepare('INSERT INTO shopping_list (barcode, name, quantity_needed, auto_generated) VALUES (?, ?, ?, 0)').run(barcode, name, quantity || 1);
    res.json({ success: true });
});

// DELETE /api/shopping/:id
app.delete('/api/shopping/:id', (req, res) => {
    db.prepare('DELETE FROM shopping_list WHERE id = ?').run(req.params.id);
    res.json({ success: true });
});

// POST /api/manual
app.post('/api/manual', (req, res) => {
    const { barcode, name, brand, category, quantity, expiry_date } = req.body;
    db.prepare('INSERT INTO products (barcode, name, brand, category, quantity, expiry_date) VALUES (?, ?, ?, ?, ?, ?)').run(barcode, name, brand, category, quantity || 1, expiry_date);
    res.json({ success: true });
});

// PUT /api/product/:id
app.put('/api/product/:id', (req, res) => {
    const { name, brand, category, quantity, expiry_date } = req.body;
    db.prepare('UPDATE products SET name = ?, brand = ?, category = ?, quantity = ?, expiry_date = ? WHERE id = ?').run(name, brand, category, quantity, expiry_date, req.params.id);
    res.json({ success: true });
});

// DELETE /api/product/:id
app.delete('/api/product/:id', (req, res) => {
    const p = db.prepare('SELECT image_path FROM products WHERE id = ?').get(req.params.id);
    if (p && p.image_path) {
        const imgFile = 'uploads/products/' + path.basename(p.image_path);
        if (fs.existsSync(imgFile)) fs.unlinkSync(imgFile);
    }
    db.prepare('DELETE FROM products WHERE id = ?').run(req.params.id);
    res.json({ success: true });
});

// GET /api/stats
app.get('/api/stats', (req, res) => {
    const total = db.prepare('SELECT COUNT(*) as count FROM products WHERE finished = 0').get();
    const expiring = db.prepare("SELECT COUNT(*) as count FROM products WHERE finished = 0 AND expiry_date IS NOT NULL AND date(expiry_date) <= date('now', '+7 days')").get();
    const shopping = db.prepare('SELECT COUNT(*) as count FROM shopping_list WHERE purchased = 0').get();
    res.json({ total_products: total.count, expiring_soon: expiring.count, shopping_items: shopping.count });
});

// Cron: Check expiring daily at 9 AM
cron.schedule('0 9 * * *', () => {
    console.log('[CRON] Checking expiring products...');
    const exp = db.prepare("SELECT * FROM products WHERE finished = 0 AND expiry_date IS NOT NULL AND date(expiry_date) <= date('now', '+3 days')").all();
    if (exp.length > 0) console.log('[CRON] ' + exp.length + ' products expiring soon!');
});

app.listen(PORT, () => console.log('Smart Fridge Server on port ' + PORT));
