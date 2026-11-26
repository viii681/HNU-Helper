const SESSION_KEY = 'hnutong.session_token';
const LOG_KEY = 'hnutong.scrubbed_log';
const LOG_MAX = 200;

export function getSessionToken(): string {
  try {
    return localStorage.getItem(SESSION_KEY) || '';
  } catch {
    return '';
  }
}

export function setSessionToken(tok: string): void {
  try {
    if (tok) localStorage.setItem(SESSION_KEY, tok);
    else localStorage.removeItem(SESSION_KEY);
  } catch {
    // ignore storage quota errors
  }
}

// 客户端第一道脱敏防线（落入 localStorage 的每一行日志均已抹除学号、密码、令牌、邮箱与长数字）
export function scrubClientText(raw: string, maxLen = 300): string {
  return String(raw ?? '')
    .replace(/\d{3,4}\*{2,}\d{2,4}/g, '#')
    .replace(/[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+/g, '#')
    .replace(/\b(password|passwd|pwd|token|sign|auth|session|cookie)=([^&\s]+)/gi, '$1=#')
    .replace(/\b(\d{1,3}\.\d{1,3})\.\d{1,3}\.\d{1,3}\b/g, '$1.*.*')
    .replace(/\d{5,}/g, '#')
    .replace(/\s+/g, ' ')
    .trim()
    .slice(0, maxLen);
}

export function appendAppLog(msg: string): void {
  try {
    const raw = localStorage.getItem(LOG_KEY);
    const list: string[] = raw ? JSON.parse(raw) : [];
    const now = new Date();
    const pad = (n: number) => (n < 10 ? '0' + n : String(n));
    const stamp = `${pad(now.getMonth() + 1)}-${pad(now.getDate())} ${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
    list.push(`${stamp} ${scrubClientText(msg, 280)}`);
    while (list.length > LOG_MAX) list.shift();
    localStorage.setItem(LOG_KEY, JSON.stringify(list));
  } catch {
    // 日志异常绝不影响主业务
  }
}

export function getAppLogText(): string {
  try {
    const raw = localStorage.getItem(LOG_KEY);
    const list: string[] = raw ? JSON.parse(raw) : [];
    return list.join('\n');
  } catch {
    return '';
  }
}

export async function apiRequest<T = any>(
  path: string,
  body?: Record<string, any>,
  method?: 'GET' | 'POST',
  opts?: { silentLog?: boolean }
): Promise<T> {
  const actualMethod = method || (body !== undefined ? 'POST' : 'GET');
  const headers: Record<string, string> = {};
  const tok = getSessionToken();
  if (tok) {
    headers['Authorization'] = `Bearer ${tok}`;
  }
  if (body !== undefined) {
    headers['Content-Type'] = 'application/json';
  }

  try {
    const resp = await fetch(path, {
      method: actualMethod,
      headers,
      credentials: 'include',
      body: body !== undefined ? JSON.stringify(body) : undefined,
    });
    const text = await resp.text();
    let data: any = null;
    try {
      data = text ? JSON.parse(text) : null;
    } catch {
      data = null;
    }
    if (!data || typeof data !== 'object') {
      const isProxyDown = resp.status === 502 || resp.status === 503 || resp.status === 504;
      const errText = isProxyDown
        ? `无法连接本地海大通后端服务（HTTP ${resp.status}），当前显示本地缓存数据`
        : `服务器返回异常响应（HTTP ${resp.status}）`;
      if (!opts?.silentLog) {
        appendAppLog(`API非JSON响应 ${path}: HTTP ${resp.status}`);
      }
      return {
        ok: false,
        status: resp.status,
        error: errText,
        kind: 'net',
        localServerDown: isProxyDown,
      } as unknown as T;
    }
    if (!resp.ok && data.ok !== false) {
      data.ok = false;
      if (!data.error) data.error = `请求失败（HTTP ${resp.status}）`;
    }
    data.status = resp.status;
    if (!data?.ok && !opts?.silentLog) {
      appendAppLog(`API失败 ${path}: ${data?.error || resp.status}`);
    }
    return data as T;
  } catch (err: any) {
    const errMsg = err?.message || String(err);
    if (!opts?.silentLog) {
      appendAppLog(`网络异常 ${path}: ${errMsg}`);
    }
    const isLocalDown = /failed to fetch|networkerror|load failed|err_connection/i.test(errMsg);
    return {
      ok: false,
      error: isLocalDown
        ? '无法连接本地海大通服务（请确认 start_local.bat / 本地后端进程正在运行），当前显示本地缓存数据'
        : `无法连接服务器（${errMsg}），当前显示本地缓存数据`,
      kind: 'net',
      localServerDown: isLocalDown,
    } as unknown as T;
  }
}
