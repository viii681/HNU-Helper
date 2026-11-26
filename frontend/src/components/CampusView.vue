<template>
  <!-- ==================== 二级页 1：海南大学通知公告与台风预警分类 ==================== -->
  <div v-if="store.subPage === 'notice'" class="campus-root">
    <header class="page-header sub-header">
      <button type="button" class="icon-btn" @click="store.closeSubPage()">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6" /></svg>
        校园
      </button>
      <h1>海大通知公告</h1>
      <button
        type="button"
        class="icon-btn"
        :class="{ spin: noticeRefreshing }"
        @click="forceRefreshNotice"
      >
        <svg viewBox="0 0 24 24">
          <path d="M21 12a9 9 0 0 0-15.5-6.36L3 8" />
          <path d="M3 3v5h5" />
          <path d="M3 12a9 9 0 0 0 15.5 6.36L21 16" />
          <path d="M16 16h5v5" />
        </svg>
      </button>
    </header>

    <!-- 五档语义分级芯片（置顶识别海南台风防汛/停课/停水电） -->
    <div class="chip-bar">
      <button
        v-for="t in NOTICE_TIERS"
        :key="t.key"
        type="button"
        class="chip"
        :class="{ active: noticeTier === t.key }"
        @click="noticeTier = t.key"
      >
        {{ t.label }}
      </button>
    </div>

    <div class="page-scroll">
      <div style="margin-bottom: 10px">
        <input
          v-model.trim="noticeKw"
          type="search"
          class="input"
          placeholder="筛选通知标题、本科生院、台风、缓考、选课…"
        />
      </div>

      <SyncProgressBar
        v-if="noticeRefreshing && store.progressTask.active"
        variant="inline"
      />

      <div v-if="store.noticeMeta.stale" class="notice-partial-tip">
        当前暂未连通海大官网，已为您保留并展示本地缓存的 {{ store.notices.length }} 条公告；可检查网络或代理后点右上角刷新。
      </div>
      <div v-else-if="(store.noticeMeta.failed || []).length > 0" class="notice-partial-tip">
        共接入 6 个海大官网源，本次 {{ store.noticeMeta.failed.length }} 个栏目响应较慢，已合并可用源与本地缓存。
      </div>

      <div v-if="filteredNotices.length === 0" class="card padded empty-state">
        <h2>该分类下暂无通知</h2>
        <p>可切换至「全部」或点击右上角实时抓取海大官网最新发文。</p>
      </div>
      <div v-else class="card">
        <button
          v-for="(nt, idx) in filteredNotices"
          :key="nt.url + idx"
          type="button"
          class="list-row tap"
          @click="openNoticeDetail(nt)"
        >
          <div style="flex: 1">
            <div class="row-title strong">
              <span
                v-for="tg in nt.tags || []"
                :key="tg"
                class="nt-tag"
                :class="nt.tier"
              >{{ tg }}</span>
              {{ nt.title }}
            </div>
            <div class="row-sub">
              {{ nt.source || nt.dept }} · {{ nt.date }}
            </div>
          </div>
          <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
        </button>
      </div>
    </div>

    <!-- 通知正文摘要提取阅读器 -->
    <div v-if="activeNotice" class="sheet-backdrop" @click.self="activeNotice = null">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="activeNotice = null">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">{{ activeNotice.title }}</h3>
        <p class="sheet-sub">{{ activeNotice.source }} · {{ activeNotice.date }}</p>

        <div v-if="noticeDetailLoading" class="empty-state"><p>正在提取通知正文关键信息…</p></div>
        <div v-else-if="noticeDetail" class="detail-box">
          <div v-if="noticeDetail.when" class="kv-pill"><strong>涉及时间：</strong>{{ noticeDetail.when }}</div>
          <div v-if="noticeDetail.where" class="kv-pill"><strong>涉及地点：</strong>{{ noticeDetail.where }}</div>
          <p class="summary-p">{{ noticeDetail.summary || '本通知正文以图片或附件形式发布，请点击下方按钮前往海南大学官网查看完整原文。' }}</p>

          <div v-if="(noticeDetail.attachments || []).length > 0" class="att-list">
            <a
              v-for="(att, ai) in noticeDetail.attachments"
              :key="ai"
              :href="att.url"
              target="_blank"
              rel="noopener noreferrer"
              class="att-item"
            >
              <span>{{ att.name }}</span>
              <span class="att-dl">打开 ↗</span>
            </a>
          </div>
        </div>
        <div v-else class="detail-box">
          <p class="summary-p" style="color: var(--sub)">
            {{ noticeDetailError || '暂未能从海南大学官网提取该公告正文（可能当前网络暂不可达或页面为纯图片/附件格式），您可重试或直接点击下方按钮打开官网原文。' }}
          </p>
          <button
            type="button"
            class="btn block"
            style="margin-top: 10px"
            @click="openNoticeDetail(activeNotice!)"
          >
            重新提取正文
          </button>
        </div>

        <a
          :href="activeNotice.url"
          target="_blank"
          rel="noopener noreferrer"
          class="btn primary block"
          style="margin-top: 14px; text-decoration: none"
        >
          打开海南大学官网原文 ↗
        </a>
      </div>
    </div>
  </div>

  <!-- ==================== 二级页 2：海南大学真实校内服务与 WebVPN 导航 ==================== -->
  <div v-else-if="store.subPage === 'services'" class="campus-root">
    <header class="page-header sub-header">
      <button type="button" class="icon-btn" @click="store.closeSubPage()">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6" /></svg>
        校园
      </button>
      <h1>校内服务与 WebVPN 导航</h1>
      <span style="width: 56px"></span>
    </header>

    <div class="page-scroll">
      <!-- 校外 WebVPN 通道卡片 -->
      <div class="card padded">
        <div class="row-title strong">校外访问内网系统？先开免插件 WebVPN</div>
        <div class="row-sub" style="margin-top: 4px">
          海南大学「网上服务大厅 (ehall)」与「正版软件中心」限校园网内网访问；若您当前使用手机流量或在家，点击下方按钮用统一身份认证登录 WebVPN 即可直接进入所有内网系统。
        </div>
        <a
          href="https://webvpn.hainanu.edu.cn/##login?cas_login=true"
          target="_blank"
          rel="noopener noreferrer"
          class="btn primary block"
          style="margin-top: 12px; text-decoration: none"
        >
          登录海南大学 WebVPN（免插件校外通道）↗
        </a>
      </div>

      <!-- 第一组：公网直连系统（实测外网 200 OK） -->
      <div class="group-label">公网直连服务（流量 / 校外网络均可直接打开）</div>
      <div class="card">
        <a
          v-for="item in PUBLIC_SERVICES"
          :key="item.name"
          :href="item.url"
          target="_blank"
          rel="noopener noreferrer"
          class="list-row tap"
          style="text-decoration: none"
        >
          <div>
            <div class="row-title strong">
              <span class="net-badge pub">公网直连</span>
              {{ item.name }}
            </div>
            <div class="row-sub">{{ item.desc }}</div>
          </div>
          <span class="row-end">打开 ↗</span>
        </a>
      </div>

      <!-- 第二组：校园网 / WebVPN 内部业务系统 -->
      <div class="group-label">校园内网系统（连校园 Wi-Fi 直达，校外请从上方 WebVPN 进入）</div>
      <div class="card">
        <a
          v-for="item in INTRANET_SERVICES"
          :key="item.name"
          :href="item.url"
          target="_blank"
          rel="noopener noreferrer"
          class="list-row tap"
          style="text-decoration: none"
        >
          <div>
            <div class="row-title strong">
              <span class="net-badge lan">校园网 / WebVPN</span>
              {{ item.name }}
            </div>
            <div class="row-sub">{{ item.desc }}</div>
          </div>
          <span class="row-end">进入 ↗</span>
        </a>
      </div>
    </div>
  </div>

  <!-- ==================== 校园主页：最近考试倒计时 + 今日课程速览 + 通知 + 高频服务 ==================== -->
  <div v-else class="campus-root">
    <header class="page-header">
      <h1>校园</h1>
      <button type="button" class="icon-btn" @click="store.openSubPage('services')">
        服务与 WebVPN
      </button>
    </header>

    <div class="page-scroll">
      <!-- 顶部双核心真实数据卡：最近考试倒计时 + 今日课程速览 -->
      <div class="dual-hero-grid">
        <!-- 左卡：最近考试倒计时卡片 -->
        <button
          type="button"
          class="card padded hero-metric-card"
          :class="{ 'warn-surface': nearestExam && nearestExam.days <= 7 }"
          @click="jumpToExamList"
        >
          <div class="hm-top">
            <span class="hm-label">最近考试倒计时</span>
            <span
              class="status-dot"
              :class="!store.authenticated ? '' : nearestExam ? (nearestExam.days <= 7 ? 'warn' : 'ok') : ''"
            ></span>
          </div>

          <template v-if="!store.authenticated">
            <div class="hm-value muted">去绑定</div>
            <div class="hm-sub">绑定海大学号后自动倒计时期末考场与座号</div>
          </template>
          <template v-else-if="nearestExam">
            <div class="hm-value">
              {{ nearestExam.days === 0 ? '今天开考' : nearestExam.days }}
              <small v-if="nearestExam.days > 0">天后</small>
            </div>
            <div class="hm-course-name">{{ nearestExam.exam.name }}</div>
            <div class="hm-sub">
              {{ nearestExam.exam.date }} {{ nearestExam.exam.arrange }}
              · {{ nearestExam.exam.room || '考场待定' }}{{ nearestExam.exam.seat ? ` 座${nearestExam.exam.seat}` : '' }}
            </div>
          </template>
          <template v-else>
            <div class="hm-value" style="font-size: 20px">暂无待考</div>
            <div class="hm-sub">当前暂无未结束的排考科目 · 点击查看考场与评教通道</div>
          </template>
        </button>

        <!-- 右卡：今日课程速览 -->
        <button
          type="button"
          class="card padded hero-metric-card"
          @click="jumpToSchedule"
        >
          <div class="hm-top">
            <span class="hm-label">今日课程速览（第{{ store.currentWeekOfToday }}周）</span>
            <span class="status-dot" :class="todayClasses.length > 0 ? 'ok' : ''"></span>
          </div>

          <template v-if="!store.authenticated && !store.timetable">
            <div class="hm-value muted">未同步</div>
            <div class="hm-sub">绑定后实时提示今日剩余节次与上课教室</div>
          </template>
          <template v-else-if="todayClasses.length > 0">
            <div class="hm-value">{{ todayClasses.length }} <small>大节</small></div>
            <div class="hm-course-name">{{ nextOrLastTodayClass.title }}</div>
            <div class="hm-sub">{{ nextOrLastTodayClass.sub }}</div>
          </template>
          <template v-else>
            <div class="hm-value" style="font-size: 20px">今日无课</div>
            <div class="hm-sub">全天自由支配 · 点击查看本周课表与日程安排</div>
          </template>
        </button>
      </div>

      <!-- 海大台风/停水电置顶提醒横幅（若最新通知中存在 impact 级公告） -->
      <button
        v-if="latestImpactNotice"
        type="button"
        class="card padded impact-alert-card"
        @click="store.openSubPage('notice')"
      >
        <div class="impact-badge-line">
          <span class="nt-tag impact">影响出行 / 重点关注</span>
          <span class="row-sub">{{ latestImpactNotice.date }}</span>
        </div>
        <div class="row-title strong" style="margin-top: 4px">{{ latestImpactNotice.title }}</div>
      </button>

      <!-- 核心频道入口列表 -->
      <div class="group-label">校园资讯与常用网办</div>
      <div class="card">
        <button type="button" class="list-row tap" @click="store.openSubPage('notice')">
          <div>
            <div class="row-title strong">海南大学通知公告</div>
            <div class="row-sub">
              实时聚合本科生院、教务运转、海大主站与讲坛（已收 {{ store.notices.length }} 条）
            </div>
          </div>
          <div class="row-end">
            <span>查看</span>
            <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
          </div>
        </button>

        <button type="button" class="list-row tap" @click="store.openSubPage('services')">
          <div>
            <div class="row-title strong">校内服务与 WebVPN 校外直达</div>
            <div class="row-sub">WebVPN 免插件通道、CARSI 知网文献、智课平台、教育邮箱、网上办事大厅</div>
          </div>
          <div class="row-end">
            <span>进入</span>
            <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
          </div>
        </button>

        <button type="button" class="list-row tap" @click="showAcademiesSheet = true">
          <div>
            <div class="row-title strong">五校区与十七书院名录</div>
            <div class="row-sub">
              <template v-if="store.profile?.academy">
                我的书院：{{ store.profile.academy }} · 覆盖全部 {{ store.allAcademiesList.length }} 个书院
              </template>
              <template v-else>
                海甸（11）、观澜湖（2）、儋州（2）、城西（1）、崖州（1）共 17 书院
              </template>
            </div>
          </div>
          <div class="row-end">
            <span>查看</span>
            <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
          </div>
        </button>
      </div>

      <!-- 海大高频生活服务四宫格 -->
      <div class="group-label">海大高频服务指南</div>
      <div class="squares-grid">
        <button
          v-for="sq in quickSquares"
          :key="sq.id"
          type="button"
          class="card padded sq-card"
          @click="activeSquare = sq"
        >
          <div class="row-title strong">{{ sq.title }}</div>
          <div class="row-sub">{{ sq.sub }}</div>
        </button>
      </div>
    </div>

    <!-- 海大四宫格服务指引与跳转弹层 -->
    <div v-if="activeSquare" class="sheet-backdrop" @click.self="activeSquare = null">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="activeSquare = null">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">{{ activeSquare.title }}</h3>
        <p class="sheet-sub">{{ activeSquare.sub }}</p>
        <div class="detail-box">
          <p class="summary-p">{{ activeSquare.guide }}</p>
        </div>
        <a
          v-if="activeSquare.url"
          :href="activeSquare.url"
          target="_blank"
          rel="noopener noreferrer"
          class="btn primary block"
          style="margin-top: 14px; text-decoration: none"
        >
          前往对应入口 ↗
        </a>
      </div>
    </div>

    <!-- 海南大学五校区与全部 17 书院分布抽屉 -->
    <div v-if="showAcademiesSheet" class="sheet-backdrop" @click.self="showAcademiesSheet = false">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="showAcademiesSheet = false">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">海南大学五校区与十七书院全景</h3>
        <p class="sheet-sub">
          <template v-if="store.profile?.academy">
            当前已绑定书院：<strong>{{ store.profile.academy }}</strong>（{{ store.profile.campus || '海甸校区' }}）
          </template>
          <template v-else>
            共 5 个校区 / 教学点、17 个书院（可在「我的 → 学籍卡片」绑定您的所属书院）
          </template>
        </p>

        <div v-for="grp in store.campusAcademiesList" :key="grp.campusId" class="campus-ac-card">
          <div class="campus-ac-title">
            <span>{{ grp.campusName }}</span>
            <span class="row-sub" style="margin: 0">{{ grp.academies.length }} 个书院</span>
          </div>
          <div class="campus-ac-pills">
            <span
              v-for="ac in grp.academies"
              :key="ac"
              class="campus-ac-pill"
              :class="{ mine: store.profile?.academy === ac }"
            >
              {{ ac }}<template v-if="store.profile?.academy === ac"> · 我的</template>
            </span>
          </div>
        </div>

        <button
          v-if="store.authenticated && !store.profile?.academy"
          type="button"
          class="btn primary block"
          style="margin-top: 14px"
          @click="showAcademiesSheet = false; store.switchTab('me')"
        >
          前往「我的」设置我的所属书院
        </button>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { apiRequest } from '../api/client';
import { useAppStore } from '../stores/app';
import type { ExamItem, NoticeItem } from '../types';
import SyncProgressBar from './SyncProgressBar.vue';

const store = useAppStore();

// ==================== 1. 最近考试倒计时计算 ====================
function calcDaysFromToday(ymd: string): number {
  if (!ymd) return -999;
  const [y, m, d] = ymd.replace(/\//g, '-').slice(0, 10).split('-').map(Number);
  if (!y || !m || !d) return -999;
  const target = new Date(y, m - 1, d);
  const now = new Date();
  const today0 = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  return Math.round((target.getTime() - today0.getTime()) / 86400000);
}

const nearestExam = computed<{ exam: ExamItem; days: number } | null>(() => {
  const list = store.exams?.exams || [];
  const upcoming: Array<{ exam: ExamItem; days: number }> = [];
  for (const ex of list) {
    const days = calcDaysFromToday(ex.date);
    if (days >= 0) {
      upcoming.push({ exam: ex, days });
    }
  }
  upcoming.sort((a, b) => a.days - b.days);
  return upcoming.length > 0 ? upcoming[0] : null;
});

function jumpToExamList() {
  if (!store.authenticated) {
    store.switchTab('me');
    return;
  }
  store.studyTargetSub = 'exams';
  store.switchTab('study');
}

// ==================== 2. 今日课程速览计算 ====================
const nowMinutes = ref<number>(new Date().getHours() * 60 + new Date().getMinutes());
let campusClockTimer: number | undefined;
onMounted(() => {
  campusClockTimer = window.setInterval(() => {
    const d = new Date();
    nowMinutes.value = d.getHours() * 60 + d.getMinutes();
  }, 30000);
});
onUnmounted(() => {
  if (campusClockTimer) {
    window.clearInterval(campusClockTimer);
    campusClockTimer = undefined;
  }
});

const todayClasses = computed(() => {
  const wk = store.currentWeekOfToday;
  const todayIdx = (new Date().getDay() + 6) % 7; // 0=周一..6=周日
  const blocks: Array<{ name: string; room: string; startP: number; endP: number }> = [];

  for (const c of store.currentTermCourses) {
    if (c.weeks && c.weeks.length > 0 && !c.weeks.includes(wk)) continue;
    const ps = (c.slots || [])
      .filter((s) => s.day === todayIdx)
      .map((s) => s.period)
      .sort((a, b) => a - b);
    if (ps.length === 0) continue;
    let i = 0;
    while (i < ps.length) {
      const s = ps[i];
      let e = s;
      while (i + 1 < ps.length && ps[i + 1] === e + 1) {
        i++;
        e = ps[i];
      }
      blocks.push({ name: c.name, room: c.room, startP: s, endP: e });
      i++;
    }
  }
  blocks.sort((a, b) => a.startP - b.startP);
  return blocks;
});

const nextOrLastTodayClass = computed(() => {
  const list = todayClasses.value;
  if (list.length === 0) return { title: '', sub: '' };
  const nowMin = nowMinutes.value;
  const bell = store.activeBellPeriods;

  const toMin = (hm: string) => {
    const [h, m] = (hm || '00:00').split(':').map(Number);
    return (h || 0) * 60 + (m || 0);
  };

  for (const b of list) {
    const sHm = bell[b.startP]?.[0] || '';
    const eHm = bell[b.endP]?.[1] || '';
    if (nowMin <= toMin(eHm)) {
      const statusPrefix = nowMin >= toMin(sHm) ? '正在上课' : `下一节 ${sHm}`;
      return {
        title: b.name,
        sub: `${statusPrefix} · 第${b.startP + 1}-${b.endP + 1}节 · ${b.room || '地点见课表'}`,
      };
    }
  }
  const last = list[list.length - 1];
  return {
    title: '今日课程已全部上完',
    sub: `最后一节：${last.name} · 点击查看明日课表`,
  };
});

function jumpToSchedule() {
  if (!store.authenticated && !store.timetable) {
    store.switchTab('me');
    return;
  }
  store.switchTab('schedule');
}

// ==================== 3. 经实测连通验证的海大真实校内服务导航 ====================
const PUBLIC_SERVICES = [
  {
    name: '海南大学强智教务一体化平台',
    desc: '正选补退选、缓考重修申请、学籍异动与官方成绩单导出',
    url: 'https://jxgl.hainanu.edu.cn/jsxsd/',
  },
  {
    name: '海南大学图书馆官网与空间预约',
    desc: '海甸北馆/南馆、儋州馆、观澜湖馆馆藏检索与研讨间预约',
    url: 'https://lib.hainanu.edu.cn/',
  },
  {
    name: 'CARSI 校外学术文献免 VPN 直连',
    desc: '使用海大统一身份认证在校外直接免费下载知网 CNKI、IEEE、Web of Science 论文',
    url: 'https://its.hainanu.edu.cn/info/1106/1874.htm',
  },
  {
    name: '海南大学智课平台',
    desc: '查看智慧教室课程录播回放、教学资源与 AI 课程助教',
    url: 'https://aic.hainanu.edu.cn/',
  },
  {
    name: '海南大学校园百事通',
    desc: '学生事务常见问题解答、各学院书院办公电话与办事流程查询',
    url: 'https://hainanu.campusphere.net/wec-amp-boya/pc/index.html?is_mobile=0',
  },
  {
    name: '海南大学学生教育邮箱 (@hainanu.edu.cn)',
    desc: '登录海大教育邮箱（可用于免费认证 GitHub Education、JetBrains 等学生包）',
    url: 'https://mail.hainanu.edu.cn/',
  },
  {
    name: '教育邮箱自助开通与密码重置',
    desc: '大一新生首次开通 @hainanu.edu.cn 邮箱别名或修改邮箱客户端密码',
    url: 'https://dcloud.hainanu.edu.cn/email/index.html',
  },
  {
    name: '统一身份认证密码找回',
    desc: '忘记教务/WebVPN/办事大厅统一密码时通过绑定手机号自助重置',
    url: 'https://authserver.hainanu.edu.cn/retrieve-password/retrievePassword/index.html',
  },
];

const INTRANET_SERVICES = [
  {
    name: '海南大学网上服务大厅 (ehall)',
    desc: '日常请假销假、离岛报备、智慧后勤报修、第二课堂、奖助学金（校外请先登上方 WebVPN）',
    url: 'https://ehall.hainanu.edu.cn/',
  },
  {
    name: '海南大学正版软件平台',
    desc: '免费下载激活校内正版 Windows、Office、WPS 365 与 MATLAB（限校园网或 WebVPN）',
    url: 'https://ms.hainanu.edu.cn/',
  },
];

// ==================== 4. 通知公告状态 ====================
const NOTICE_TIERS = [
  { key: 'all', label: '全部' },
  { key: 'impact', label: '影响出行 · 台风停水电' },
  { key: 'action', label: '要办事' },
  { key: 'activity', label: '讲座活动' },
  { key: 'other', label: '常规教务' },
];
const noticeTier = ref('all');
const noticeKw = ref('');
const noticeRefreshing = ref(false);
const activeNotice = ref<NoticeItem | null>(null);
const noticeDetailLoading = ref(false);
const noticeDetail = ref<any>(null);
const noticeDetailError = ref('');
const showAcademiesSheet = ref(false);

const latestImpactNotice = computed(() =>
  store.notices.find((n) => n.tier === 'impact') || null
);

const filteredNotices = computed(() => {
  const kw = noticeKw.value.toLowerCase();
  return store.notices.filter((n) => {
    if (noticeTier.value !== 'all' && n.tier !== noticeTier.value) return false;
    if (
      kw &&
      !n.title.toLowerCase().includes(kw) &&
      !(n.source || '').toLowerCase().includes(kw) &&
      !(n.dept || '').toLowerCase().includes(kw)
    ) {
      return false;
    }
    return true;
  });
});

async function forceRefreshNotice() {
  noticeRefreshing.value = true;
  try {
    const res = await store.refreshNotices(true);
    if (!res?.ok) {
      store.showToast(res?.error || '刷新海南大学通知公告失败，请稍后重试');
    } else if (res.stale) {
      store.showToast('海大官网响应超时，已安全保留本地缓存公告');
    } else {
      store.showToast('已同步海南大学各官网最新通知');
    }
  } finally {
    noticeRefreshing.value = false;
  }
}

let noticeDetailSeq = 0;

async function openNoticeDetail(nt: NoticeItem) {
  const seq = ++noticeDetailSeq;
  activeNotice.value = nt;
  noticeDetail.value = null;
  noticeDetailError.value = '';
  noticeDetailLoading.value = true;
  try {
    const res = await apiRequest(`/api/v1/notice/detail?u=${encodeURIComponent(nt.url)}`);
    if (seq !== noticeDetailSeq || activeNotice.value?.url !== nt.url) return;
    if (res?.ok) {
      noticeDetail.value = res;
    } else {
      noticeDetailError.value = res?.error || '暂无法连接海南大学官网提取正文，请点击下方按钮打开原文';
    }
  } finally {
    if (seq === noticeDetailSeq) {
      noticeDetailLoading.value = false;
    }
  }
}

const DEFAULT_SQUARES = [
  {
    id: 'webvpn',
    title: '校外免插件 WebVPN',
    sub: '校外进办事大厅 · 查内网',
    url: 'https://webvpn.hainanu.edu.cn/##login?cas_login=true',
    guide: '在校外使用流量或宽带时，点击下方按钮登录海南大学免插件 WebVPN（账号密码与教务一致），即可直接访问网上服务大厅 (ehall)、正版软件平台及校内电子图书资源。',
  },
  {
    id: 'lib',
    title: '图书馆与 CARSI 文献',
    sub: '座位研讨间 · 知网直连',
    url: 'https://lib.hainanu.edu.cn/',
    guide: '海甸北馆/南馆、儋州馆、观澜湖馆研修间支持在线预约；校外下载知网 CNKI / IEEE 论文可使用学校 CARSI 联邦认证免 VPN 直连。',
  },
  {
    id: 'edu_mail',
    title: '海大教育邮箱',
    sub: '@hainanu.edu.cn 邮箱登录',
    url: 'https://mail.hainanu.edu.cn/',
    guide: '直接打开海南大学官方教育邮箱（@hainanu.edu.cn）登录页，用于收发邮件、学术投稿及认证 GitHub Student Pack、JetBrains 全家桶学生免费授权；首次开通或找回密码可在登录页下方点击「邮箱注册 / 忘记密码」。',
  },
];

const quickSquares = computed(() => DEFAULT_SQUARES);
const activeSquare = ref<any>(null);
</script>

<style scoped>
.campus-root {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}

.dual-hero-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
  margin-bottom: 12px;
}

@media (max-width: 520px) {
  .dual-hero-grid {
    grid-template-columns: 1fr;
  }
}

.hero-metric-card {
  margin-bottom: 0;
  text-align: left;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.hm-top {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.hm-label {
  font-size: 12px;
  color: var(--sub);
}

.hm-value {
  font-size: 26px;
  font-weight: 700;
  line-height: 1.18;
  color: var(--accent);
}

.hm-value small {
  font-size: 14px;
  font-weight: 600;
}

.hm-value.muted {
  font-size: 20px;
}

.hm-course-name {
  font-size: 14px;
  font-weight: 700;
  color: var(--text);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.hm-sub {
  font-size: 12px;
  color: var(--sub);
  line-height: 1.35;
}

.impact-alert-card {
  text-align: left;
  width: 100%;
  border-left: 4px solid var(--danger);
}

.impact-badge-line {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.nt-tag {
  display: inline-block;
  padding: 1px 6px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 600;
  margin-right: 6px;
  background: var(--fill);
  color: var(--sub);
}

.nt-tag.impact {
  background: var(--danger-bg);
  color: var(--danger);
}

.nt-tag.action {
  background: var(--accent-soft);
  color: var(--accent);
}

.nt-tag.activity {
  background: rgba(52, 199, 89, 0.15);
  color: var(--ok);
}

.net-badge {
  display: inline-block;
  padding: 1px 6px;
  border-radius: 4px;
  font-size: 11px;
  font-weight: 600;
  margin-right: 6px;
}

.net-badge.pub {
  background: rgba(52, 199, 89, 0.15);
  color: var(--ok);
}

.net-badge.lan {
  background: var(--accent-soft);
  color: var(--accent);
}

.squares-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
}

.sq-card {
  margin-bottom: 0;
  text-align: left;
}

.sq-card:last-child:nth-child(odd) {
  grid-column: 1 / -1;
}

.detail-box {
  background: var(--fill);
  padding: 14px;
  border-radius: var(--r-card);
}

.kv-pill {
  font-size: 13px;
  margin-bottom: 6px;
}

.campus-ac-card {
  background: var(--fill);
  border-radius: var(--r-card);
  padding: 12px;
  margin-bottom: 10px;
}

.campus-ac-title {
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 13px;
  font-weight: 700;
  margin-bottom: 8px;
}

.campus-ac-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.campus-ac-pill {
  padding: 4px 10px;
  border-radius: 999px;
  background: var(--card);
  color: var(--text);
  font-size: 12px;
  font-weight: 500;
}

.campus-ac-pill.mine {
  background: var(--accent);
  color: #fff;
  font-weight: 700;
}

.summary-p {
  margin: 0;
  font-size: 14px;
  line-height: 1.65;
  white-space: pre-wrap;
}

.att-list {
  margin-top: 10px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.att-item {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: 8px 10px;
  border-radius: var(--r-list);
  background: var(--card);
  color: var(--text);
  font-size: 13px;
  text-decoration: none;
}

.att-dl {
  color: var(--accent);
  font-weight: 600;
  flex-shrink: 0;
}

.notice-partial-tip {
  font-size: 12px;
  color: var(--sub);
  margin-bottom: 8px;
  padding: 0 4px;
}
</style>
