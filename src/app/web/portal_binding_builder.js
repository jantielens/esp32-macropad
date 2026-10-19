// Shared by source-specific builders; discovery stays with the owning scheme.
function bindingBuilderUnitSuffix(unit) {
    return unit.trim() ? ' ' + unit.trim() : '';
}

function bindingBuilderFormat(mode, decimals, suffix) {
    if (mode === 'raw') return '';
    if (!['number', 'text'].includes(mode)) throw new Error('Choose a valid formatting mode.');
    if (mode === 'number' && (!Number.isInteger(decimals) || decimals < 0 || decimals > 6)) {
        throw new Error('Choose 0 to 6 decimal places.');
    }
    if (/[;[\]|]/.test(suffix)) throw new Error('The suffix cannot contain binding delimiters (; [ ] |).');
    const format = (mode === 'number' ? '%.' + decimals + 'f' : '%s') + suffix.replace(/%/g, '%%');
    if (new TextEncoder().encode(format).length >= 64) throw new Error('The format must be shorter than 64 UTF-8 bytes.');
    return format;
}

function bindingBuilderToken(scheme, source, path, format, fallback) {
    if (!/^[a-z][a-z0-9_]*$/.test(scheme) || !source || /[;[\]|]/.test(source)) {
        throw new Error('Enter a valid binding source.');
    }
    if (/[;[\]|]/.test(path)) throw new Error('The path cannot contain binding delimiters (; [ ] |).');
    if (/[;[\]|]/.test(fallback)) throw new Error('The fallback cannot contain binding delimiters (; [ ] |).');
    const params = [source];
    if (path || format) params.push(path);
    if (format) params.push(format);
    const token = '[' + scheme + ':' + params.join(';') + (fallback ? '|' + fallback : '') + ']';
    if (new TextEncoder().encode(token).length >= 192) throw new Error('The binding must be shorter than 192 UTF-8 bytes.');
    return token;
}

function bindingBuilderSample(value, mode, decimals, suffix) {
    if (value == null) return null;
    if (mode === 'raw') return String(value);
    if (mode === 'text') return String(value) + suffix;
    // Do not turn an empty string, boolean, or partially numeric text into a number.
    if (!/^[+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?$/i.test(String(value).trim())) return String(value);
    const number = Number(value);
    return Number.isFinite(number) ? number.toFixed(decimals) + suffix : String(value);
}
