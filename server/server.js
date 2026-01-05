const express = require('express');
const cors = require('cors');
const multer = require('multer');
const Database = require('better-sqlite3');
const Tesseract = require('tesseract.js');
const cron = require('node-cron');
const path = require('path');
const fs = require('fs');
const helmet = require('helmet');
const rateLimit = require('express-rate-limit');
const basicAuth = require('express-basic-auth');

const app = express();
const PORT = process.env.PORT || 3000;

// Trust nginx proxy
app.set('trust proxy', 1);

// ============ SECURITY MIDDLEWARE ============

// Helmet per security headers
app.use(helmet({
    contentSecurityPolicy: {
        directives: {
            defaultSrc: ["'self'"],
            styleSrc: ["'self'", "'unsafe-inline'"],
            scriptSrc: ["'self'", "'unsafe-inline'"],
            scriptSrcAttr: ["'unsafe-inline'"],  // Permette onclick inline
            imgSrc: ["'self'", "data:", "blob:"],
            connectSrc: ["'self'"],
        },
    },
    crossOriginEmbedderPolicy: false,
}));

// Rate limiting - max 100 richieste per minuto per IP
const limiter = rateLimit({
    windowMs: 60 * 1000,
    max: 100,
    message: { error: 'Troppe richieste, riprova tra un minuto' },
    standardHeaders: true,
    legacyHeaders: false,
});
app.use('/api/', limiter);

// CORS restrittivo
app.use(cors({
    origin: ['https://frigo.xamad.net', 'http://localhost:3000'],
    methods: ['GET', 'POST', 'PUT', 'DELETE'],
    allowedHeaders: ['Content-Type', 'Authorization'],
}));

app.use(express.json({ limit: '1mb' }));

// ============ BASIC AUTH PER WEB UI ============
const authMiddleware = basicAuth({
    users: { 'admin': 'pippobaudo' },
    challenge: true,
    realm: 'Smart Fridge Admin',
});

// Proteggi pagine web UI (non API per scanner)
app.use('/', (req, res, next) => {
    // API endpoints accessibili senza auth (per scanner barcode)
    if (req.path.startsWith('/api/')) {
        return next();
    }
    // Immagini accessibili senza auth
    if (req.path.startsWith('/images/')) {
        return next();
    }
    // Web UI richiede auth
    authMiddleware(req, res, next);
});

app.use(express.static('public'));
app.use('/images', express.static('uploads/products'));

// ============ FILE UPLOAD SICURO ============
const ALLOWED_MIMETYPES = ['image/jpeg', 'image/png', 'image/gif', 'image/webp'];
const ALLOWED_EXTENSIONS = ['.jpg', '.jpeg', '.png', '.gif', '.webp'];

const storage = multer.diskStorage({
    destination: (req, file, cb) => {
        const dir = 'uploads/products';
        if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
        cb(null, dir);
    },
    filename: (req, file, cb) => {
        // Genera nome sicuro senza usare filename originale
        const ext = path.extname(file.originalname).toLowerCase();
        const safeName = Date.now() + '-' + Math.random().toString(36).substring(2, 15) + ext;
        cb(null, safeName);
    }
});

const fileFilter = (req, file, cb) => {
    const ext = path.extname(file.originalname).toLowerCase();
    if (ALLOWED_MIMETYPES.includes(file.mimetype) && ALLOWED_EXTENSIONS.includes(ext)) {
        cb(null, true);
    } else {
        cb(new Error('Tipo file non permesso'), false);
    }
};

const upload = multer({
    storage,
    limits: { fileSize: 5 * 1024 * 1024 },
    fileFilter
});

// ============ DATABASE ============
const db = new Database('fridge.db');
db.exec(`
    CREATE TABLE IF NOT EXISTS products (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        barcode TEXT NOT NULL,
        name TEXT,
        brand TEXT,
        category TEXT,
        quantity INTEGER DEFAULT 1,
        weight TEXT,
        purchase_date TEXT,
        expiry_date TEXT,
        image_path TEXT,
        added_date TEXT DEFAULT CURRENT_TIMESTAMP,
        finished INTEGER DEFAULT 0,
        from_receipt INTEGER DEFAULT 0
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

// ============ INPUT VALIDATION ============
function sanitizeString(str, maxLen = 255) {
    if (!str || typeof str !== 'string') return null;
    return str.trim().substring(0, maxLen).replace(/[<>\"']/g, '');
}

function validateBarcode(barcode) {
    if (!barcode || typeof barcode !== 'string') return null;
    // Solo numeri e lettere, max 50 caratteri
    return barcode.replace(/[^a-zA-Z0-9-]/g, '').substring(0, 50);
}

function validateDate(dateStr) {
    if (!dateStr) return null;
    // Formato YYYY-MM-DD o DD/MM/YYYY
    const match = dateStr.match(/^(\d{4}-\d{2}-\d{2}|\d{2}\/\d{2}\/\d{4})$/);
    return match ? dateStr : null;
}

function validatePositiveInt(val, defaultVal = 1, max = 9999) {
    const num = parseInt(val);
    if (isNaN(num) || num < 1) return defaultVal;
    return Math.min(num, max);
}

// ============ API ENDPOINTS ============

app.post('/api/product', upload.single('image'), (req, res) => {
    try {
        const action = sanitizeString(req.body.action, 20);
        const barcode = validateBarcode(req.body.barcode);
        const expiry_date = validateDate(req.body.expiry_date);
        const device = sanitizeString(req.body.device, 100);
        const imagePath = req.file ? '/images/' + req.file.filename : null;

        if (!barcode) {
            return res.status(400).json({ success: false, error: 'Barcode non valido' });
        }
        if (!['add', 'remove'].includes(action)) {
            return res.status(400).json({ success: false, error: 'Azione non valida' });
        }

        console.log('[PRODUCT] ' + action + ': ' + barcode);

        db.prepare('INSERT INTO scan_history (barcode, action, device, image_path) VALUES (?, ?, ?, ?)').run(barcode, action, device, imagePath);

        if (action === 'add') {
            const purchaseDate = new Date().toISOString().split('T')[0]; // Today's date
            const existing = db.prepare('SELECT * FROM products WHERE barcode = ? AND finished = 0').get(barcode);
            if (existing) {
                db.prepare('UPDATE products SET quantity = quantity + 1, image_path = COALESCE(?, image_path) WHERE id = ?').run(imagePath, existing.id);
            } else {
                db.prepare('INSERT INTO products (barcode, expiry_date, purchase_date, image_path) VALUES (?, ?, ?, ?)').run(barcode, expiry_date, purchaseDate, imagePath);
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
                        const imgFile = path.join('uploads/products', path.basename(existing.image_path));
                        if (fs.existsSync(imgFile)) fs.unlinkSync(imgFile);
                    }
                    if (!db.prepare('SELECT * FROM shopping_list WHERE barcode = ?').get(barcode)) {
                        db.prepare('INSERT INTO shopping_list (barcode, name, auto_generated) VALUES (?, ?, 1)').run(barcode, existing.name || barcode);
                    }
                }
            }
            res.json({ success: true, message: 'Prodotto rimosso' });
        }
    } catch (e) {
        console.error('[ERROR] /api/product:', e.message);
        res.status(500).json({ success: false, error: 'Errore interno' });
    }
});

app.post('/api/ocr', upload.single('image'), async (req, res) => {
    if (!req.file) return res.status(400).json({ error: 'Nessuna immagine' });
    try {
        const { data: { text } } = await Tesseract.recognize(req.file.path, 'ita+eng');
        const datePatterns = [/(\d{2})[\/\-\.](\d{2})[\/\-\.](\d{4})/g, /(\d{2})[\/\-\.](\d{2})[\/\-\.](\d{2})/g];
        let expiry_date = null;
        for (const p of datePatterns) {
            const m = text.match(p);
            if (m) { expiry_date = m[0]; break; }
        }
        // Cleanup file temporaneo
        try { fs.unlinkSync(req.file.path); } catch (e) {}
        res.json({ expiry_date, confidence: expiry_date ? 0.7 : 0 });
    } catch (e) {
        console.error('[ERROR] /api/ocr:', e.message);
        res.status(500).json({ error: 'OCR fallito' });
    }
});

// Receipt scanning endpoint - parses deli receipts (scontrini gastronomia)
app.post('/api/receipt', upload.single('image'), async (req, res) => {
    if (!req.file) return res.status(400).json({ error: 'Nessuna immagine' });
    try {
        console.log('[RECEIPT] Processing receipt image...');
        const { data: { text } } = await Tesseract.recognize(req.file.path, 'ita+eng');
        console.log('[RECEIPT] OCR text:', text.substring(0, 500));

        // Parse purchase date from receipt
        let purchaseDate = new Date().toISOString().split('T')[0]; // Default: today
        const datePatterns = [
            /(\d{2})[\/\-\.](\d{2})[\/\-\.](\d{4})/,  // DD/MM/YYYY
            /(\d{2})[\/\-\.](\d{2})[\/\-\.](\d{2})/   // DD/MM/YY
        ];
        for (const pattern of datePatterns) {
            const match = text.match(pattern);
            if (match) {
                const day = match[1];
                const month = match[2];
                let year = match[3];
                if (year.length === 2) year = '20' + year;
                purchaseDate = `${year}-${month}-${day}`;
                break;
            }
        }

        // Calculate expiry date: purchase_date + 4 days (for deli products)
        const expiryDate = new Date(purchaseDate);
        expiryDate.setDate(expiryDate.getDate() + 4);
        const expiryDateStr = expiryDate.toISOString().split('T')[0];

        // Parse products from receipt
        // Common patterns: "PROSCIUTTO CRUDO 0,150 kg € 3,50" or "PROSCIUTTO COTTO    2.50"
        const lines = text.split('\n').filter(l => l.trim().length > 3);
        const products = [];

        // Keywords that indicate deli products
        const deliKeywords = [
            'prosciutto', 'salame', 'mortadella', 'coppa', 'bresaola', 'speck',
            'formaggio', 'parmigiano', 'grana', 'pecorino', 'mozzarella', 'ricotta',
            'gorgonzola', 'provolone', 'asiago', 'fontina', 'taleggio', 'stracchino',
            'carne', 'manzo', 'maiale', 'pollo', 'tacchino', 'vitello', 'agnello',
            'salsiccia', 'wurstel', 'pancetta', 'guanciale', 'lardo', 'bacon',
            'arrosto', 'roast', 'cotoletta', 'hamburger', 'macinato', 'tritato',
            'olive', 'sottoli', 'antipasto', 'insalata', 'preparato'
        ];

        // Weight patterns: "0,150 kg" or "150 g" or "0.250kg"
        const weightPattern = /(\d+[,\.]\d+)\s*(kg|g|gr|hg)/i;
        // Price pattern: "€ 3,50" or "3.50" or "EUR 3,50"
        const pricePattern = /(?:€|EUR|eur)?\s*(\d+[,\.]\d{2})/;

        for (const line of lines) {
            const lineLower = line.toLowerCase();

            // Check if line contains a deli product keyword
            const matchedKeyword = deliKeywords.find(kw => lineLower.includes(kw));
            if (matchedKeyword) {
                // Extract weight if present
                const weightMatch = line.match(weightPattern);
                const weight = weightMatch ? `${weightMatch[1]} ${weightMatch[2]}` : null;

                // Clean product name (remove prices, weights, codes)
                let name = line
                    .replace(weightPattern, '')
                    .replace(pricePattern, '')
                    .replace(/\d{8,}/g, '')  // Remove barcodes
                    .replace(/[€$]/g, '')
                    .trim()
                    .substring(0, 100);

                // Skip if name is too short or just numbers
                if (name.length < 3 || /^\d+$/.test(name)) continue;

                // Generate a unique receipt-based barcode
                const receiptBarcode = 'RCP' + Date.now() + Math.random().toString(36).substring(2, 6).toUpperCase();

                products.push({
                    barcode: receiptBarcode,
                    name: name,
                    weight: weight,
                    purchase_date: purchaseDate,
                    expiry_date: expiryDateStr,
                    from_receipt: 1,
                    category: matchedKeyword.includes('formaggio') || matchedKeyword.includes('mozza') ? 'Latticini' :
                              matchedKeyword.includes('carne') || matchedKeyword.includes('manzo') || matchedKeyword.includes('maiale') ? 'Carne' :
                              'Salumi'
                });
            }
        }

        // Insert products into database
        const insertStmt = db.prepare(`
            INSERT INTO products (barcode, name, category, weight, purchase_date, expiry_date, from_receipt)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        `);

        for (const product of products) {
            insertStmt.run(
                product.barcode,
                product.name,
                product.category,
                product.weight,
                product.purchase_date,
                product.expiry_date,
                product.from_receipt
            );
            console.log('[RECEIPT] Added:', product.name, product.weight || '');
        }

        // Cleanup temp file
        try { fs.unlinkSync(req.file.path); } catch (e) {}

        res.json({
            success: true,
            purchase_date: purchaseDate,
            expiry_date: expiryDateStr,
            products_found: products.length,
            products: products.map(p => ({ name: p.name, weight: p.weight, category: p.category }))
        });
    } catch (e) {
        console.error('[ERROR] /api/receipt:', e.message);
        try { fs.unlinkSync(req.file.path); } catch (e2) {}
        res.status(500).json({ error: 'Elaborazione scontrino fallita' });
    }
});

app.get('/api/inventory', (req, res) => {
    try {
        const products = db.prepare('SELECT * FROM products WHERE finished = 0 ORDER BY expiry_date ASC').all();
        res.json({ products, count: products.length });
    } catch (e) {
        res.status(500).json({ error: 'Errore database' });
    }
});

app.get('/api/expiring', (req, res) => {
    try {
        // FIX SQL INJECTION: usa prepared statement invece di interpolazione
        const days = validatePositiveInt(req.query.days, 7, 365);
        const stmt = db.prepare(`
            SELECT * FROM products
            WHERE finished = 0 AND expiry_date IS NOT NULL
            AND date(expiry_date) <= date('now', '+' || ? || ' days')
            ORDER BY expiry_date ASC
        `);
        const products = stmt.all(days);
        res.json({ products, count: products.length, days });
    } catch (e) {
        console.error('[ERROR] /api/expiring:', e.message);
        res.status(500).json({ error: 'Errore database' });
    }
});

app.get('/api/shopping', (req, res) => {
    try {
        const items = db.prepare('SELECT * FROM shopping_list WHERE purchased = 0 ORDER BY added_date DESC').all();
        res.json({ shopping_list: items, total_items: items.length });
    } catch (e) {
        res.status(500).json({ error: 'Errore database' });
    }
});

app.post('/api/shopping', (req, res) => {
    try {
        const barcode = validateBarcode(req.body.barcode);
        const name = sanitizeString(req.body.name, 200);
        const quantity = validatePositiveInt(req.body.quantity, 1, 999);

        if (!barcode && !name) {
            return res.status(400).json({ success: false, error: 'Barcode o nome richiesto' });
        }

        db.prepare('INSERT INTO shopping_list (barcode, name, quantity_needed, auto_generated) VALUES (?, ?, ?, 0)')
            .run(barcode || '', name || barcode, quantity);
        res.json({ success: true });
    } catch (e) {
        res.status(500).json({ success: false, error: 'Errore database' });
    }
});

app.delete('/api/shopping/:id', (req, res) => {
    try {
        const id = validatePositiveInt(req.params.id, 0);
        if (!id) return res.status(400).json({ success: false });
        db.prepare('DELETE FROM shopping_list WHERE id = ?').run(id);
        res.json({ success: true });
    } catch (e) {
        res.status(500).json({ success: false });
    }
});

// Modifica item lista spesa
app.put('/api/shopping/:id', (req, res) => {
    try {
        const id = validatePositiveInt(req.params.id, 0);
        if (!id) return res.status(400).json({ success: false });

        const updates = [];
        const params = [];

        if (req.body.name !== undefined) {
            updates.push('name = ?');
            params.push(sanitizeString(req.body.name, 200));
        }
        if (req.body.quantity_needed !== undefined) {
            updates.push('quantity_needed = ?');
            params.push(validatePositiveInt(req.body.quantity_needed, 1, 999));
        }

        if (updates.length === 0) {
            return res.status(400).json({ success: false, error: 'Nessun campo da aggiornare' });
        }

        params.push(id);
        db.prepare(`UPDATE shopping_list SET ${updates.join(', ')} WHERE id = ?`).run(...params);
        res.json({ success: true });
    } catch (e) {
        res.status(500).json({ success: false, error: 'Errore database' });
    }
});

// Rimuovi dalla lista spesa per nome (per comandi vocali)
app.post('/api/shopping/remove', (req, res) => {
    try {
        const name = sanitizeString(req.body.name, 200);
        if (!name) {
            return res.status(400).json({ success: false, error: 'Nome richiesto' });
        }
        // Cerca con LIKE case-insensitive
        const item = db.prepare('SELECT id FROM shopping_list WHERE LOWER(name) LIKE ? AND purchased = 0').get('%' + name.toLowerCase() + '%');
        if (!item) {
            return res.json({ success: false, not_found: true });
        }
        db.prepare('DELETE FROM shopping_list WHERE id = ?').run(item.id);
        res.json({ success: true });
    } catch (e) {
        res.status(500).json({ success: false, error: 'Errore database' });
    }
});

// Svuota lista della spesa
app.post('/api/shopping/clear', (req, res) => {
    try {
        db.prepare('DELETE FROM shopping_list WHERE purchased = 0').run();
        res.json({ success: true });
    } catch (e) {
        res.status(500).json({ success: false, error: 'Errore database' });
    }
});

app.post('/api/manual', (req, res) => {
    try {
        const barcode = validateBarcode(req.body.barcode);
        const name = sanitizeString(req.body.name, 200);
        const brand = sanitizeString(req.body.brand, 100);
        const category = sanitizeString(req.body.category, 50);
        const quantity = validatePositiveInt(req.body.quantity, 1, 999);
        const expiry_date = validateDate(req.body.expiry_date);
        const purchaseDate = new Date().toISOString().split('T')[0]; // Today's date

        if (!barcode) {
            return res.status(400).json({ success: false, error: 'Barcode richiesto' });
        }

        db.prepare('INSERT INTO products (barcode, name, brand, category, quantity, expiry_date, purchase_date) VALUES (?, ?, ?, ?, ?, ?, ?)')
            .run(barcode, name, brand, category, quantity, expiry_date, purchaseDate);
        res.json({ success: true });
    } catch (e) {
        res.status(500).json({ success: false, error: 'Errore database' });
    }
});

app.put('/api/product/:id', (req, res) => {
    try {
        const id = validatePositiveInt(req.params.id, 0);
        if (!id) return res.status(400).json({ success: false });

        const name = sanitizeString(req.body.name, 200);
        const brand = sanitizeString(req.body.brand, 100);
        const category = sanitizeString(req.body.category, 50);
        const quantity = validatePositiveInt(req.body.quantity, 1, 999);
        const weight = sanitizeString(req.body.weight, 50);
        const purchase_date = validateDate(req.body.purchase_date);
        const expiry_date = validateDate(req.body.expiry_date);

        db.prepare('UPDATE products SET name = ?, brand = ?, category = ?, quantity = ?, weight = ?, purchase_date = ?, expiry_date = ? WHERE id = ?')
            .run(name, brand, category, quantity, weight, purchase_date, expiry_date, id);
        res.json({ success: true });
    } catch (e) {
        res.status(500).json({ success: false });
    }
});

app.delete('/api/product/:id', (req, res) => {
    try {
        const id = validatePositiveInt(req.params.id, 0);
        if (!id) return res.status(400).json({ success: false });

        const p = db.prepare('SELECT image_path FROM products WHERE id = ?').get(id);
        if (p && p.image_path) {
            const imgFile = path.join('uploads/products', path.basename(p.image_path));
            if (fs.existsSync(imgFile)) fs.unlinkSync(imgFile);
        }
        db.prepare('DELETE FROM products WHERE id = ?').run(id);
        res.json({ success: true });
    } catch (e) {
        res.status(500).json({ success: false });
    }
});

app.get('/api/stats', (req, res) => {
    try {
        const total = db.prepare('SELECT COUNT(*) as count FROM products WHERE finished = 0').get();
        const expiring = db.prepare("SELECT COUNT(*) as count FROM products WHERE finished = 0 AND expiry_date IS NOT NULL AND date(expiry_date) <= date('now', '+7 days')").get();
        const shopping = db.prepare('SELECT COUNT(*) as count FROM shopping_list WHERE purchased = 0').get();
        res.json({ total_products: total.count, expiring_soon: expiring.count, shopping_items: shopping.count });
    } catch (e) {
        res.status(500).json({ error: 'Errore database' });
    }
});

// Health check endpoint (no auth)
app.get('/api/health', (req, res) => {
    res.json({ status: 'ok', timestamp: new Date().toISOString() });
});

// ============ CRON JOB ============
cron.schedule('0 9 * * *', () => {
    console.log('[CRON] Checking expiring products...');
    const exp = db.prepare("SELECT * FROM products WHERE finished = 0 AND expiry_date IS NOT NULL AND date(expiry_date) <= date('now', '+3 days')").all();
    if (exp.length > 0) console.log('[CRON] ' + exp.length + ' products expiring soon!');
});

// ============ ERROR HANDLER ============
app.use((err, req, res, next) => {
    console.error('[ERROR]', err.message);
    if (err instanceof multer.MulterError) {
        return res.status(400).json({ error: 'Errore upload: ' + err.message });
    }
    res.status(500).json({ error: 'Errore interno del server' });
});

// ============ START SERVER ============
app.listen(PORT, '127.0.0.1', () => console.log('Smart Fridge Server on port ' + PORT + ' (localhost only)'));
