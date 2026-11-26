import { defineStore } from 'pinia';
import { computed, ref } from 'vue';
import { apiRequest, appendAppLog, setSessionToken } from '../api/client';
import type {
  CampusAcademyGroup,
  CourseItem,
  CreditAnalysis,
  DormElecData,
  ExamsData,
  GradesData,
  MainTab,
  NoticeItem,
  ProgressTaskState,
  SubPage,
  TimetableData,
  UserPreferences,
  UserProfile,
} from '../types';

export const CACHE_BUNDLE_KEY = 'hnutong.bundle_cache.v1';
export const CACHE_META_KEY = 'hnutong.meta_cache.v1';
export const CACHE_NOTICES_KEY = 'hnutong.notices_cache.v1';
export const CACHE_LOG_KEY = 'hnutong.scrubbed_log';

// 海南大学五校区与全部 17 个书院官方完整字典（前端离线兜底）
export const DEFAULT_HNU_CAMPUS_ACADEMIES: CampusAcademyGroup[] = [
  {
    campusId: '1',
    campusName: '海甸校区',
    academies: [
      '椰风书院', '乘风书院', '凤翔书院', '子衿书院', '青云书院',
      '南海书院', '淳明书院', '海德书院', '天工书院', '朴诚书院', '中和书院',
    ],
  },
  {
    campusId: '3',
    campusName: '观澜湖校区',
    academies: ['今朝书院', '日新书院'],
  },
  {
    campusId: '2',
    campusName: '儋州校区',
    academies: ['丹心书院', '檀雅书院'],
  },
  {
    campusId: '5',
    campusName: '城西教学点',
    academies: ['晨曦书院'],
  },
  {
    campusId: '4',
    campusName: '崖州湾科教园',
    academies: ['崖州书院'],
  },
];

export function inferCampusByAcademy(academy: string): string {
  for (const grp of DEFAULT_HNU_CAMPUS_ACADEMIES) {
    if (grp.academies.includes(academy)) return grp.campusName;
  }
  return '';
}

// 10 色防撞色课表调色盘（兼顾浅色与深色模式下的文字对比度）
export const COURSE_PALETTE = [
  { bg: '#DBEAFE', fg: '#1E40AF', darkBg: '#1E3A5F', darkFg: '#BFDBFE' },
  { bg: '#DCFCE7', fg: '#166534', darkBg: '#18422B', darkFg: '#BBF7D0' },
  { bg: '#FEF3C7', fg: '#92400E', darkBg: '#4A3512', darkFg: '#FDE68A' },
  { bg: '#FCE7F3', fg: '#9D174D', darkBg: '#4E1D38', darkFg: '#FBCFE8' },
  { bg: '#EDE9FE', fg: '#5B21B6', darkBg: '#34205E', darkFg: '#DDD6FE' },
  { bg: '#CCFBF1', fg: '#115E59', darkBg: '#153F3D', darkFg: '#99F6E4' },
  { bg: '#FFEDD5', fg: '#9A3412', darkBg: '#4C2512', darkFg: '#FED7AA' },
  { bg: '#E0E7FF', fg: '#3730A3', darkBg: '#23275E', darkFg: '#C7D2FE' },
  { bg: '#FAE8FF', fg: '#86198F', darkBg: '#461A4B', darkFg: '#F5D0FE' },
  { bg: '#FFE4E6', fg: '#9F1239', darkBg: '#4E1927', darkFg: '#FECDD3' },
];

export const DEFAULT_HNU_BELL: string[][] = [
  ['07:40', '08:25'],
  ['08:35', '09:20'],
  ['09:45', '10:30'],
  ['10:40', '11:25'],
  ['14:30', '15:15'],
  ['15:25', '16:10'],
  ['16:35', '17:20'],
  ['17:30', '18:15'],
  ['19:20', '20:05'],
  ['20:15', '21:00'],
  ['21:10', '21:55'],
];

export const useAppStore = defineStore('app', () => {
  // 导航与全局反馈
  const activeTab = ref<MainTab>('schedule');
  const subPage = ref<SubPage>('');
  const studyTargetSub = ref<string>('');
  const toastMsg = ref('');
  let toastTimer: number | undefined;

  // 公共学校元数据
  const meta = ref<any>(loadLocal(CACHE_META_KEY, null));

  // 登录与用户业务快照
  const authenticated = ref(false);
  const credState = ref<'none' | 'ok' | 'expired' | 'broken'>('none');
  const credBriefs = ref<Record<string, any>>({});
  const sessionTtlDays = ref(365);
  const latestCaptcha = ref<any>(null);
  const profile = ref<UserProfile | null>(null);
  const icsToken = ref('');
  const syncedAt = ref(0);
  const staleTimetableWarn = ref('');

  const timetable = ref<TimetableData | null>(null);
  const historyTimetable = ref<TimetableData | null>(null); // 非 null 表示正在翻阅历史学期课表
  const grades = ref<GradesData | null>(null);
  const exams = ref<ExamsData | null>(null);
  const semesters = ref<Array<{ id: string; name: string }>>([]);
  const credits = ref<CreditAnalysis | null>(null);
  const cxTodo = ref<any>(null);
  const moocTodo = ref<any>(null);
  const portalCard = ref<any>(null);
  const portalApps = ref<any[] | null>(null);
  const dormElec = ref<DormElecData | null>(null);
  const cachedNoticesInit = loadLocal(CACHE_NOTICES_KEY, null);
  const notices = ref<NoticeItem[]>(Array.isArray(cachedNoticesInit?.items) ? cachedNoticesInit.items : []);
  const noticeMeta = ref<{ at: number; sources: number; failed: any[]; stale?: boolean }>({
    at: cachedNoticesInit?.at || 0,
    sources: cachedNoticesInit?.sources ?? 6,
    failed: cachedNoticesInit?.failed || [],
    stale: false,
  });

  // 全局实时数据同步 / 上传更新进度条引擎
  const progressTask = ref<ProgressTaskState>({
    active: false,
    id: '',
    title: '',
    stage: '',
    detail: '',
    percent: 0,
    step: 1,
    totalSteps: 1,
    status: 'running',
    startedAt: 0,
    elapsedSec: 0,
    kind: 'sync',
  });
  let progTickTimer: number | undefined;
  let progPollTimer: number | undefined;
  let progHideTimer: number | undefined;
  let progAutoCreepMax = 88;
  let progHasServerUpdate = false;
  let progPollInFlight = false;
  let progActiveToken = '';

  function createSyncId(prefix = 'sync'): string {
    try {
      const bytes = new Uint8Array(16);
      crypto.getRandomValues(bytes);
      const hex = Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('');
      return `${prefix}_${hex}`;
    } catch {
      return `${prefix}_${Date.now()}_${Math.random().toString(36).slice(2, 12)}${Math.random().toString(36).slice(2, 12)}`;
    }
  }

  function clearProgressTimers() {
    if (progTickTimer) {
      window.clearInterval(progTickTimer);
      progTickTimer = undefined;
    }
    if (progPollTimer) {
      window.clearInterval(progPollTimer);
      progPollTimer = undefined;
    }
    if (progHideTimer) {
      window.clearTimeout(progHideTimer);
      progHideTimer = undefined;
    }
    progPollInFlight = false;
  }

  function startProgressTask(opts: {
    title: string;
    stage: string;
    detail?: string;
    totalSteps?: number;
    kind?: 'sync' | 'upload' | 'cache' | 'scan';
    pollSyncId?: string;
    initialPercent?: number;
    autoCreepTo?: number;
  }): string {
    clearProgressTimers();
    const now = Date.now();
    const taskId = opts.pollSyncId || createSyncId(opts.kind || 'task');
    progActiveToken = taskId;
    progAutoCreepMax = opts.autoCreepTo ?? 90;
    progHasServerUpdate = false;
    progressTask.value = {
      active: true,
      id: taskId,
      title: opts.title,
      stage: opts.stage,
      detail: opts.detail || '',
      percent: opts.initialPercent ?? 8,
      step: 1,
      totalSteps: opts.totalSteps ?? 1,
      status: 'running',
      startedAt: now,
      elapsedSec: 0,
      kind: opts.kind || 'sync',
    };

    // 每 100ms 更新实时耗时与平滑微推进动画，让用户清晰感知同步/上传正在活跃运行而非卡死
    progTickTimer = window.setInterval(() => {
      if (progActiveToken !== taskId || !progressTask.value.active || progressTask.value.status !== 'running') return;
      progressTask.value.elapsedSec = (Date.now() - progressTask.value.startedAt) / 1000;
      const cur = progressTask.value.percent;
      const ceiling = progHasServerUpdate ? Math.min(96, cur + 6) : progAutoCreepMax;
      if (cur < ceiling) {
        const delta = Math.max(0.18, (ceiling - cur) * 0.045);
        progressTask.value.percent = Math.min(ceiling, cur + delta);
      }
    }, 100);

    // 若绑定了后端 syncId，则每 260ms 实时轮询 C++ 后端真实阶段与百分比（带防重入锁与静默日志防刷屏）
    if (opts.pollSyncId) {
      const sid = opts.pollSyncId;
      progPollTimer = window.setInterval(async () => {
        if (
          progPollInFlight ||
          progActiveToken !== taskId ||
          !progressTask.value.active ||
          progressTask.value.status !== 'running'
        ) {
          return;
        }
        progPollInFlight = true;
        try {
          const r = await apiRequest(`/api/v1/sync/progress?id=${encodeURIComponent(sid)}`, undefined, 'GET', {
            silentLog: true,
          });
          if (r?.ok && r.found && progActiveToken === taskId && progressTask.value.id === sid && progressTask.value.status === 'running') {
            progHasServerUpdate = true;
            if (typeof r.percent === 'number' && r.percent > progressTask.value.percent) {
              progressTask.value.percent = r.percent;
            }
            if (typeof r.step === 'number' && r.step > 0) progressTask.value.step = r.step;
            if (typeof r.totalSteps === 'number' && r.totalSteps > 0) progressTask.value.totalSteps = r.totalSteps;
            if (r.stage) progressTask.value.stage = r.stage;
            if (r.detail !== undefined) progressTask.value.detail = r.detail;
          }
        } finally {
          progPollInFlight = false;
        }
      }, 260);
    }
    return taskId;
  }

  function updateProgressTask(patch: Partial<ProgressTaskState>, taskToken?: string) {
    if (!progressTask.value.active) return;
    if (taskToken && progActiveToken !== taskToken) return;
    if (typeof patch.percent === 'number' && patch.percent >= progressTask.value.percent) {
      progressTask.value.percent = patch.percent;
    }
    if (patch.step !== undefined) progressTask.value.step = patch.step;
    if (patch.totalSteps !== undefined) progressTask.value.totalSteps = patch.totalSteps;
    if (patch.stage !== undefined) progressTask.value.stage = patch.stage;
    if (patch.detail !== undefined) progressTask.value.detail = patch.detail;
    if (patch.title !== undefined) progressTask.value.title = patch.title;
  }

  function finishProgressTask(success: boolean, finalStage?: string, finalDetail?: string, taskToken?: string) {
    if (taskToken && progActiveToken !== taskToken) return;
    const finishedId = progressTask.value.id;
    if (progTickTimer) {
      window.clearInterval(progTickTimer);
      progTickTimer = undefined;
    }
    if (progPollTimer) {
      window.clearInterval(progPollTimer);
      progPollTimer = undefined;
    }
    progPollInFlight = false;
    if (!progressTask.value.active) return;
    progressTask.value.elapsedSec = (Date.now() - progressTask.value.startedAt) / 1000;
    progressTask.value.percent = 100;
    progressTask.value.status = success ? 'done' : 'error';
    if (success) progressTask.value.step = progressTask.value.totalSteps;
    if (finalStage) progressTask.value.stage = finalStage;
    if (finalDetail !== undefined) progressTask.value.detail = finalDetail;

    const holdMs = success ? 1300 : 2400;
    if (progHideTimer) window.clearTimeout(progHideTimer);
    progHideTimer = window.setTimeout(() => {
      if (progressTask.value.id === finishedId) {
        progressTask.value.active = false;
      }
    }, holdMs);
  }

  async function runTrackedTask<T>(
    opts: {
      title: string;
      stage: string;
      detail?: string;
      totalSteps?: number;
      kind?: 'sync' | 'upload' | 'cache' | 'scan';
      doneStage?: string;
      failStage?: string;
    },
    fn: () => Promise<T>
  ): Promise<T> {
    const token = startProgressTask({
      title: opts.title,
      stage: opts.stage,
      detail: opts.detail,
      totalSteps: opts.totalSteps ?? 1,
      kind: opts.kind || 'upload',
      initialPercent: 18,
      autoCreepTo: 88,
    });
    try {
      const result = await fn();
      const ok = result === undefined || result === null || (result as any)?.ok !== false;
      finishProgressTask(
        ok,
        ok ? opts.doneStage || '已完成更新' : opts.failStage || (result as any)?.error || '操作未完成',
        undefined,
        token
      );
      return result;
    } catch (err: any) {
      finishProgressTask(false, opts.failStage || err?.message || '请求异常', undefined, token);
      throw err;
    }
  }

  function defaultPreferences(): UserPreferences {
    return {
      campusPreset: 'haidian',
      termStartDate: '2026-08-31',
      totalWeeks: 20,
      bellSchedule: [],
      hiddenCourses: [],
      hiddenSlots: [],
      customCourses: [],
      courseColors: {},
      doneMarks: {},
      seenItems: {},
      cardLowThreshold: 20,
      elecLowThreshold: 15,
      remindCfg: { courseBeforeMin: 15, workBeforeHours: 6, enabled: false },
      telemetryOptIn: true,
    };
  }

  const preferences = ref<UserPreferences>(defaultPreferences());

  // 单飞同步锁：防止连点多次触发上游防抖
  const syncing = ref(false);

  function loadLocal(key: string, fallback: any) {
    try {
      const s = localStorage.getItem(key);
      return s ? JSON.parse(s) : fallback;
    } catch {
      return fallback;
    }
  }

  function saveLocal(key: string, val: any) {
    try {
      localStorage.setItem(key, JSON.stringify(val));
    } catch {
      // ignore
    }
  }

  function showToast(msg: string, durationMs = 2600) {
    toastMsg.value = msg;
    if (toastTimer) window.clearTimeout(toastTimer);
    toastTimer = window.setTimeout(() => {
      toastMsg.value = '';
    }, durationMs);
  }

  function switchTab(tab: MainTab) {
    activeTab.value = tab;
    subPage.value = '';
  }

  function openSubPage(sp: SubPage) {
    subPage.value = sp;
  }

  function closeSubPage() {
    subPage.value = '';
  }

  function resetUserState(opts?: { keepAuth?: boolean; clearLocal?: boolean }) {
    if (!opts?.keepAuth) authenticated.value = false;
    credState.value = 'none';
    credBriefs.value = {};
    latestCaptcha.value = null;
    profile.value = null;
    icsToken.value = '';
    syncedAt.value = 0;
    staleTimetableWarn.value = '';
    timetable.value = null;
    historyTimetable.value = null;
    grades.value = null;
    exams.value = null;
    semesters.value = [];
    credits.value = null;
    cxTodo.value = null;
    moocTodo.value = null;
    portalCard.value = null;
    portalApps.value = null;
    dormElec.value = null;
    preferences.value = defaultPreferences();
    if (opts?.clearLocal) {
      localStorage.removeItem(CACHE_BUNDLE_KEY);
    }
  }

  function applyBundle(b: any) {
    if (!b) return;
    const prevSid = profile.value?.studentId || '';
    const nextSid = b.profile?.studentId || '';
    const switchedUser = Boolean(prevSid && nextSid && prevSid !== nextSid);
    if (switchedUser) {
      // 同浏览器切换至另一学号时，彻底清空前一位用户的全部内存状态与本地离线快照，严防串号残留
      resetUserState({ keepAuth: true, clearLocal: true });
    }

    authenticated.value = !!b.authenticated;
    if (b.profile) profile.value = b.profile;
    if (b.icsToken !== undefined) icsToken.value = b.icsToken || '';
    if (b.credState) credState.value = b.credState;
    if (b.credBriefs) credBriefs.value = b.credBriefs;
    if (b.preferences) {
      preferences.value = {
        ...defaultPreferences(),
        ...(switchedUser ? {} : preferences.value),
        ...b.preferences,
      };
    }
    if (b.dormElec !== undefined) dormElec.value = b.dormElec;
    if (typeof b.syncedAt === 'number') syncedAt.value = b.syncedAt;
    if (b.timetable !== undefined) {
      const nextCourses = Array.isArray(b.timetable?.courses) ? b.timetable.courses : [];
      const curCourses = Array.isArray(timetable.value?.courses) ? timetable.value!.courses : [];
      // 前端同样加一道防空覆盖红线（仅限同一用户）：不把已有课程的本地课表覆盖为空课表
      const sameUser = !switchedUser && (!nextSid || !prevSid || nextSid === prevSid);
      if (!(sameUser && nextCourses.length === 0 && curCourses.length > 0)) {
        timetable.value = b.timetable;
      }
    }
    if (b.grades !== undefined) grades.value = b.grades;
    if (b.exams !== undefined) exams.value = b.exams;
    if (Array.isArray(b.semesters)) semesters.value = b.semesters;
    if (b.credits !== undefined) credits.value = b.credits;
    if (b.cxTodo !== undefined) cxTodo.value = b.cxTodo;
    if (b.moocTodo !== undefined) moocTodo.value = b.moocTodo;
    if (b.portalCard !== undefined) portalCard.value = b.portalCard;
    if (b.portalApps !== undefined) portalApps.value = b.portalApps;
    staleTimetableWarn.value = b.timetableError || '';

    saveLocal(CACHE_BUNDLE_KEY, {
      authenticated: authenticated.value,
      profile: profile.value,
      icsToken: icsToken.value,
      credState: credState.value,
      credBriefs: credBriefs.value,
      preferences: preferences.value,
      dormElec: dormElec.value,
      syncedAt: syncedAt.value,
      timetable: timetable.value,
      grades: grades.value,
      exams: exams.value,
      semesters: semesters.value,
      credits: credits.value,
      cxTodo: cxTodo.value,
      moocTodo: moocTodo.value,
      portalCard: portalCard.value,
      portalApps: portalApps.value,
    });
  }

  async function initApp() {
    // 1. 先从本地离线缓存秒级恢复界面（不限于必须有课表，兼容仅缓存档案/成绩/偏好的情况）
    const cached = loadLocal(CACHE_BUNDLE_KEY, null);
    if (cached && typeof cached === 'object') {
      applyBundle(cached);
    }

    // 2. 并行拉取海大公共元数据与服务端实时会话快照
    const [metaRes, sessRes] = await Promise.all([
      apiRequest('/api/v1/meta/bootstrap'),
      apiRequest('/api/v1/auth/session'),
    ]);
    if (metaRes?.ok) {
      meta.value = metaRes;
      saveLocal(CACHE_META_KEY, metaRes);
    }
    if (sessRes?.ok) {
      if (sessRes.authenticated) {
        if (sessRes.sessionToken) setSessionToken(sessRes.sessionToken);
        if (typeof sessRes.sessionTtlDays === 'number' && sessRes.sessionTtlDays > 0) {
          sessionTtlDays.value = sessRes.sessionTtlDays;
        }
        applyBundle(sessRes);
        // 若此前缓存的课表为空且凭据状态正常，仅在后台静默尝试补拉一次，失败绝不劫持页面跳转
        if (
          credState.value === 'ok' &&
          (!timetable.value || !Array.isArray(timetable.value.courses) || timetable.value.courses.length === 0)
        ) {
          syncJwxt(true);
        }
      } else {
        // 服务端确认会话已失效/未登录：彻底清理本机令牌、内存状态与本地快照，杜绝共用设备残留前一位用户的课表与成绩
        setSessionToken('');
        resetUserState({ clearLocal: true });
      }
    }

    // 3. 后台预取校园公告
    refreshNotices(false);

    // 4. 匿名日活统计（每天仅发一次随机临时串，不含学号/设备号）
    sendDailyPing();
  }

  function sendDailyPing() {
    if (preferences.value.telemetryOptIn === false) return;
    if ((navigator as any).webdriver) return;
    try {
      const today = new Date().toISOString().slice(0, 10);
      if (localStorage.getItem('hnutong.ping_day') === today) return;
      const rnd = Math.random().toString(36).slice(2, 12);
      localStorage.setItem('hnutong.ping_day', today);
      apiRequest('/api/v1/telemetry', { k: 'dau_ping', dayId: `${today}_${rnd}` });
    } catch {
      // ignore
    }
  }

  async function refreshNotices(force = false) {
    let syncId = '';
    if (force) {
      syncId = createSyncId('notice');
      startProgressTask({
        title: '正在同步海南大学通知公告',
        stage: '并发抓取本科生院与海大主站 6 大栏目…',
        detail: '智能识别台风预警、选课与重要发文',
        totalSteps: 6,
        kind: 'sync',
        pollSyncId: syncId,
        initialPercent: 10,
        autoCreepTo: 92,
      });
    }
    try {
      const qs = force
        ? `/api/v1/notice?limit=60&force=1&syncId=${encodeURIComponent(syncId)}`
        : `/api/v1/notice?limit=60&force=0`;
      const res = await apiRequest(qs);
      if (res?.ok && Array.isArray(res.items)) {
        notices.value = res.items;
        noticeMeta.value = {
          at: res.at || 0,
          sources: res.sources ?? 6,
          failed: res.failed || [],
          stale: !!res.stale,
        };
        saveLocal(CACHE_NOTICES_KEY, {
          items: res.items,
          at: noticeMeta.value.at,
          sources: noticeMeta.value.sources,
          failed: noticeMeta.value.failed,
        });
        if (force) {
          finishProgressTask(
            true,
            res.stale
              ? `官网暂未连通，已保留 ${res.items.length} 条本地公告`
              : `已同步 ${res.items.length} 条海南大学最新公告`
          );
        }
      } else if (force) {
        finishProgressTask(false, res?.error || '抓取通知公告失败');
      }
      return res;
    } catch (err: any) {
      if (force) finishProgressTask(false, err?.message || '同步通知异常');
      return { ok: false, error: err?.message || '同步通知异常' };
    }
  }

  async function syncJwxt(silent = false): Promise<any> {
    if (syncing.value) return { ok: false, busy: true };
    syncing.value = true;
    const syncId = createSyncId('jw_sync');
    if (!silent) {
      startProgressTask({
        title: '正在同步海南大学教务数据',
        stage: '连接教务一体化平台并检查会话…',
        detail: '课表 · 成绩 · 考场 · 培养方案',
        totalSteps: 6,
        kind: 'sync',
        pollSyncId: syncId,
        initialPercent: 8,
        autoCreepTo: 92,
      });
    }
    try {
      const res = await apiRequest('/api/v1/jw/sync', { syncId });
      if (res?.ok) {
        historyTimetable.value = null;
        applyBundle({ ...res, authenticated: true });
        if (!silent) {
          finishProgressTask(true, res.timetableError ? '教务数据部分同步完成' : '海南大学教务数据已更新至最新');
          showToast(res.timetableError ? `同步部分完成（${res.timetableError}）` : '已同步海南大学教务最新数据');
        }
      } else if (res?.needCaptcha) {
        credState.value = 'expired';
        if (res.captcha?.ok) latestCaptcha.value = res.captcha;
        if (!silent) {
          finishProgressTask(false, '教务单次会话已过期', '已保留本地登录与缓存，输一次验证码即可恢复');
          showToast('教务单次会话已过期，请输入一次验证码恢复同步');
          switchTab('me');
        }
      } else if (res?.error) {
        // 网络超时绝不把本地登录状态破坏为 expired
        if (!silent) {
          finishProgressTask(false, res.kind === 'net' ? '教务网络连接超时' : '同步未能完成', res.error);
          showToast(res.error);
        }
      } else if (!silent) {
        finishProgressTask(false, '同步未返回有效响应');
      }
      return res;
    } finally {
      syncing.value = false;
    }
  }

  async function updateUserProfile(academy: string, campus?: string) {
    return runTrackedTask(
      {
        title: '正在更新学籍书院信息',
        stage: '上传所属书院与校区资料…',
        detail: academy ? `${campus || inferCampusByAcademy(academy) || '海南大学'} · ${academy}` : '更新个人资料',
        kind: 'upload',
        doneStage: '已保存所属书院信息',
      },
      async () => {
        const res = await apiRequest('/api/v1/account/profile', {
          academy,
          campus: campus || inferCampusByAcademy(academy) || profile.value?.campus || '',
        });
        if (res?.ok && res.profile) {
          profile.value = res.profile;
          const cached = loadLocal(CACHE_BUNDLE_KEY, {});
          cached.profile = res.profile;
          saveLocal(CACHE_BUNDLE_KEY, cached);
        }
        return res;
      }
    );
  }

  function getLocalCacheSummary() {
    const byteSize = (key: string) => {
      try {
        const raw = localStorage.getItem(key);
        return raw ? new Blob([raw]).size : 0;
      } catch {
        return 0;
      }
    };
    const logCount = (() => {
      try {
        const raw = localStorage.getItem(CACHE_LOG_KEY);
        const arr = raw ? JSON.parse(raw) : [];
        return Array.isArray(arr) ? arr.length : 0;
      } catch {
        return 0;
      }
    })();

    const bundleBytes = byteSize(CACHE_BUNDLE_KEY);
    const noticesBytes = byteSize(CACHE_NOTICES_KEY);
    const metaBytes = byteSize(CACHE_META_KEY);
    const logBytes = byteSize(CACHE_LOG_KEY);
    const pingBytes = byteSize('hnutong.ping_day');
    const totalBytes = bundleBytes + noticesBytes + metaBytes + logBytes + pingBytes;

    return {
      totalBytes,
      items: {
        bundle: { bytes: bundleBytes, courses: timetable.value?.courses?.length || 0 },
        notices: { bytes: noticesBytes, count: notices.value.length },
        meta: { bytes: metaBytes },
        logs: { bytes: logBytes, count: logCount },
        other: { bytes: pingBytes },
      },
    };
  }

  async function clearLocalCaches(opts: {
    bundle: boolean;
    notices: boolean;
    meta: boolean;
    logs: boolean;
    browserCache?: boolean;
    serverSnapshots: boolean;
    resyncAfter: boolean;
  }) {
    const totalSteps = (opts.serverSnapshots ? 1 : 0) + (opts.resyncAfter ? 2 : 1) + 1;
    startProgressTask({
      title: '正在清理本地缓存',
      stage: '扫描并释放本地存储空间…',
      detail: '保留账号登录状态与加密凭据',
      totalSteps,
      kind: 'cache',
      initialPercent: 12,
      autoCreepTo: 92,
    });
    try {
      let step = 1;
      if (opts.bundle) {
        localStorage.removeItem(CACHE_BUNDLE_KEY);
        historyTimetable.value = null;
      }
      if (opts.notices) {
        localStorage.removeItem(CACHE_NOTICES_KEY);
      }
      if (opts.meta) {
        localStorage.removeItem(CACHE_META_KEY);
      }
      if (opts.logs) {
        localStorage.removeItem(CACHE_LOG_KEY);
        localStorage.removeItem('hnutong.ping_day');
      }
      if (opts.browserCache && typeof caches !== 'undefined') {
        try {
          const keys = await caches.keys();
          await Promise.all(keys.map((k) => caches.delete(k)));
        } catch {
          // ignore
        }
      }
      step++;
      updateProgressTask({
        percent: opts.serverSnapshots || opts.resyncAfter ? 42 : 90,
        step,
        stage: '本地浏览器缓存已清理完毕',
        detail: '会话令牌与密码保险箱完好保留',
      });

      if (opts.serverSnapshots && authenticated.value) {
        updateProgressTask({
          percent: 58,
          stage: '正在清理云端快照缓存…',
          detail: '保留账号绑定，仅重置缓存快照',
        });
        await apiRequest('/api/v1/account/clear-cache', {});
        step++;
      }

      if (opts.resyncAfter) {
        updateProgressTask({
          percent: 72,
          step,
          stage: '正在重新加载校历、书院与通知缓存…',
          detail: '拉取最新公共元数据与公告',
        });
        const [metaRes] = await Promise.all([
          apiRequest('/api/v1/meta/bootstrap'),
          refreshNotices(false),
        ]);
        if (metaRes?.ok) {
          meta.value = metaRes;
          saveLocal(CACHE_META_KEY, metaRes);
        }
        finishProgressTask(true, '缓存已清理并重新拉取基础数据');
        if (authenticated.value) {
          await syncJwxt();
        }
      } else {
        if (opts.meta) {
          const metaRes = await apiRequest('/api/v1/meta/bootstrap');
          if (metaRes?.ok) {
            meta.value = metaRes;
            saveLocal(CACHE_META_KEY, metaRes);
          }
        }
        finishProgressTask(true, '本地缓存清理完成', '登录信息已安全保留');
      }
      showToast('已完成本地缓存清理（登录状态已保留）');
    } catch (err: any) {
      finishProgressTask(false, '缓存清理时遇到异常', err?.message || '');
      showToast('缓存清理异常，请稍后重试');
    }
  }

  const campusAcademiesList = computed<CampusAcademyGroup[]>(() => {
    const raw = meta.value?.campusesAndAcademies?.campusAcademies;
    if (Array.isArray(raw) && raw.length >= 5) return raw;
    return DEFAULT_HNU_CAMPUS_ACADEMIES;
  });

  const allAcademiesList = computed<string[]>(() => {
    const raw = meta.value?.campusesAndAcademies?.academies;
    if (Array.isArray(raw) && raw.length >= 17) return raw;
    return DEFAULT_HNU_CAMPUS_ACADEMIES.flatMap((g) => g.academies);
  });

  let prefSaveQueue: Promise<any> = Promise.resolve();
  let prefRev = 0;

  async function updatePreferences(patch: Partial<UserPreferences>): Promise<{ ok: boolean; error?: string }> {
    const myRev = ++prefRev;
    const prevSnapshot = { ...preferences.value };
    preferences.value = { ...preferences.value, ...patch };

    const run = async (): Promise<{ ok: boolean; error?: string }> => {
      if (authenticated.value) {
        const res = await apiRequest('/api/v1/preferences', patch);
        if (res?.ok && res.preferences) {
          if (myRev === prefRev) {
            preferences.value = res.preferences;
          }
          const cached = loadLocal(CACHE_BUNDLE_KEY, {});
          cached.preferences = preferences.value;
          saveLocal(CACHE_BUNDLE_KEY, cached);
          return { ok: true };
        }
        if (myRev === prefRev) {
          preferences.value = prevSnapshot;
        }
        return { ok: false, error: res?.error || '保存设置失败' };
      }
      const cached = loadLocal(CACHE_BUNDLE_KEY, {});
      cached.preferences = preferences.value;
      saveLocal(CACHE_BUNDLE_KEY, cached);
      return { ok: true };
    };

    const task = prefSaveQueue.then(run, run);
    prefSaveQueue = task.catch(() => {});
    return task;
  }

  function setIcsToken(tok: string) {
    icsToken.value = tok;
    const cached = loadLocal(CACHE_BUNDLE_KEY, {});
    if (cached && typeof cached === 'object') {
      cached.icsToken = tok;
      saveLocal(CACHE_BUNDLE_KEY, cached);
    }
  }

  async function refreshSessionBriefs() {
    if (!authenticated.value) return;
    const sessRes = await apiRequest('/api/v1/auth/session');
    if (sessRes?.ok && sessRes.authenticated) {
      if (sessRes.credBriefs) credBriefs.value = sessRes.credBriefs;
      if (sessRes.credState) credState.value = sessRes.credState;
      const cached = loadLocal(CACHE_BUNDLE_KEY, {});
      if (cached && typeof cached === 'object') {
        cached.credBriefs = credBriefs.value;
        cached.credState = credState.value;
        saveLocal(CACHE_BUNDLE_KEY, cached);
      }
    }
  }

  async function unbindAllAndLogout() {
    const res = await apiRequest('/api/v1/jw/unbind', {});
    setSessionToken('');
    resetUserState({ clearLocal: true });
    if (res?.ok || res?.needBind) {
      showToast('已解除绑定并清理本机与云端会话');
    } else {
      showToast('已清理本机登录状态（云端解绑响应异常，请稍后确认）');
    }
  }

  // 当前生效的 11 节作息表
  const activeBellPeriods = computed<string[][]>(() => {
    if (Array.isArray(preferences.value.bellSchedule) && preferences.value.bellSchedule.length >= 10) {
      return preferences.value.bellSchedule;
    }
    const presetKey = preferences.value.campusPreset || 'haidian';
    const presets = meta.value?.bellPresets;
    if (presets && presets[presetKey]?.periods) {
      return presets[presetKey].periods;
    }
    return DEFAULT_HNU_BELL;
  });

  // 当前正在查看的课表（本学期或临时翻阅的历史学期）
  const activeTimetable = computed<TimetableData | null>(() => {
    return historyTimetable.value || timetable.value;
  });

  const isViewingHistoryTerm = computed(() => historyTimetable.value !== null);

  // 计算今天对应的教学周次（1 ~ totalWeeks，采用 UTC 日历日差规避夏令时 23/25 小时漂移）
  const currentWeekOfToday = computed<number>(() => {
    const startStr = preferences.value.termStartDate || '2026-08-31';
    const [y, m, d] = startStr.split('-').map(Number);
    if (!y || !m || !d) return 1;
    const now = new Date();
    const startUtc = Date.UTC(y, m - 1, d);
    const todayUtc = Date.UTC(now.getFullYear(), now.getMonth(), now.getDate());
    const diffDays = Math.floor((todayUtc - startUtc) / 86400000);
    if (diffDays < 0) return 1;
    const wk = Math.floor(diffDays / 7) + 1;
    const maxW = preferences.value.totalWeeks || activeTimetable.value?.totalWeeks || 20;
    return Math.min(Math.max(1, wk), maxW);
  });

  function filterVisibleCourses(base: CourseItem[], includeCustom: boolean): CourseItem[] {
    const custom = includeCustom
      ? (preferences.value.customCourses || []).map((c) => ({ ...c, isCustom: true }))
      : [];
    const hiddenSet = new Set(preferences.value.hiddenCourses || []);
    const hiddenSlotNamedSet = new Set<string>();
    const hiddenSlotLegacyCodeSet = new Set<string>();
    for (const s of preferences.value.hiddenSlots || []) {
      hiddenSlotNamedSet.add(`${s.code || ''}|${s.name || ''}|${s.day}|${s.period}`);
      if (s.code && !s.name) {
        hiddenSlotLegacyCodeSet.add(`${s.code}|${s.day}|${s.period}`);
      }
    }

    const out: CourseItem[] = [];
    for (const c of [...base, ...custom]) {
      const courseKey = c.code || `NAME:${c.name}`;
      if ((c.code && hiddenSet.has(c.code)) || hiddenSet.has(courseKey)) continue;
      const filteredSlots = (c.slots || []).filter(
        (s) =>
          !hiddenSlotNamedSet.has(`${c.code || ''}|${c.name || ''}|${s.day}|${s.period}`) &&
          !(c.code && hiddenSlotLegacyCodeSet.has(`${c.code}|${s.day}|${s.period}`))
      );
      if (filteredSlots.length === 0) continue;
      out.push({ ...c, slots: filteredSlots });
    }
    return out;
  }

  // 合并教务课程 + 自定义加课，并过滤整门隐藏与单节隐藏（随历史学期切换，供课表页使用）
  const visibleCourses = computed<CourseItem[]>(() =>
    filterVisibleCourses(activeTimetable.value?.courses || [], !isViewingHistoryTerm.value)
  );

  // 始终基于当前主学期课表（不受历史学期浏览状态污染），供校园页「今日课程速览」使用
  const currentTermCourses = computed<CourseItem[]>(() =>
    filterVisibleCourses(timetable.value?.courses || [], true)
  );

  // 稳定防撞色分配（按课程名分配调色盘下标，优先读取用户手动指定的颜色）
  const courseColorMap = computed<Record<string, typeof COURSE_PALETTE[0]>>(() => {
    const map: Record<string, typeof COURSE_PALETTE[0]> = {};
    const names = Array.from(new Set(visibleCourses.value.map((c) => c.name))).sort();
    names.forEach((name, idx) => {
      const customHex = preferences.value.courseColors?.[name];
      if (customHex) {
        map[name] = { bg: customHex + '28', fg: customHex, darkBg: customHex + '44', darkFg: '#FFFFFF' };
      } else {
        map[name] = COURSE_PALETTE[idx % COURSE_PALETTE.length];
      }
    });
    return map;
  });

  // 汇总所有未交作业截止点（学习通 + MOOC）与考试安排，按 YYYY-MM-DD 归档供课表刻度渲染
  const markersByDate = computed<Record<string, Array<any>>>(() => {
    const map: Record<string, Array<any>> = {};
    const doneMarks = preferences.value.doneMarks || {};
    const nowSec = Math.floor(Date.now() / 1000);

    const pushItem = (ymd: string, entry: any) => {
      if (!map[ymd]) map[ymd] = [];
      map[ymd].push(entry);
    };

    const collectPlatform = (src: any, sourceLabel: string) => {
      if (!src || !Array.isArray(src.items)) return;
      for (const it of src.items) {
        const state = it.bucket || it.state;
        if (state !== 'todo') continue;
        if (!it.endAt) continue;
        const endSec = Number(it.endAt);
        if (!Number.isFinite(endSec) || endSec < nowSec - 86400) continue;
        const dkey = `${it.course}|${it.title}`;
        if (doneMarks[dkey] && Math.abs(doneMarks[dkey] - endSec) < 7200) continue;

        const dt = new Date(endSec * 1000);
        const ymd = `${dt.getFullYear()}-${String(dt.getMonth() + 1).padStart(2, '0')}-${String(dt.getDate()).padStart(2, '0')}`;
        const hhmm = `${String(dt.getHours()).padStart(2, '0')}:${String(dt.getMinutes()).padStart(2, '0')}`;
        pushItem(ymd, {
          markerType: it.kind === 'exam' ? 'cx_exam' : 'work_due',
          source: sourceLabel,
          course: it.course,
          title: it.title,
          hhmm,
          endAt: endSec,
          url: it.url,
        });
      }
    };

    collectPlatform(cxTodo.value, '学习通');
    collectPlatform(moocTodo.value, 'MOOC');

    for (const ex of exams.value?.exams || []) {
      if (!ex.date) continue;
      const ymd = ex.date.replace(/\//g, '-').slice(0, 10);
      pushItem(ymd, {
        markerType: 'school_exam',
        source: '教务考试',
        course: ex.name,
        title: `${ex.name}（${ex.category || '考试'}）`,
        arrange: ex.arrange || '',
        room: ex.room || '',
        campus: ex.campus || '',
        seat: ex.seat || '',
      });
    }

    return map;
  });

  return {
    activeTab,
    subPage,
    studyTargetSub,
    toastMsg,
    meta,
    authenticated,
    credState,
    credBriefs,
    sessionTtlDays,
    latestCaptcha,
    profile,
    icsToken,
    syncedAt,
    staleTimetableWarn,
    timetable,
    historyTimetable,
    grades,
    exams,
    semesters,
    credits,
    cxTodo,
    moocTodo,
    portalCard,
    portalApps,
    dormElec,
    notices,
    noticeMeta,
    preferences,
    syncing,
    progressTask,
    campusAcademiesList,
    allAcademiesList,
    activeBellPeriods,
    activeTimetable,
    isViewingHistoryTerm,
    currentWeekOfToday,
    visibleCourses,
    currentTermCourses,
    courseColorMap,
    markersByDate,
    showToast,
    switchTab,
    openSubPage,
    closeSubPage,
    applyBundle,
    initApp,
    refreshNotices,
    syncJwxt,
    updatePreferences,
    setIcsToken,
    refreshSessionBriefs,
    updateUserProfile,
    getLocalCacheSummary,
    clearLocalCaches,
    createSyncId,
    startProgressTask,
    updateProgressTask,
    finishProgressTask,
    runTrackedTask,
    unbindAllAndLogout,
    appendAppLog,
  };
});
