<template>
  <!-- ==================== 二级页：海大校历与 .ics 系统日历订阅 ==================== -->
  <div v-if="store.subPage === 'calendar'" class="subpage-wrap">
    <header class="page-header sub-header">
      <button type="button" class="icon-btn" @click="store.closeSubPage()">
        <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6" /></svg>
        课表
      </button>
      <h1>海大校历与系统订阅</h1>
      <span style="width: 56px"></span>
    </header>

    <div class="page-scroll">
      <div class="card padded">
        <div class="cal-hero-top">
          <div>
            <div class="row-sub">{{ calendarData?.year || '2026–2027' }} 学年 · 当前教学进度</div>
            <div class="cal-big-week">第 {{ store.currentWeekOfToday }} 周 <span class="cal-sub-total">/ 共 {{ totalWeeks }} 周</span></div>
          </div>
          <span class="status-dot ok">进行中</span>
        </div>
        <div class="progress-track" style="margin-top: 12px">
          <div
            class="progress-fill"
            :style="{ width: Math.min(100, Math.round((store.currentWeekOfToday / totalWeeks) * 100)) + '%' }"
          ></div>
        </div>
        <div class="row-sub" style="margin-top: 8px">
          开学首周周一：{{ store.preferences.termStartDate }} · {{ calendarData?.sourceNote || '以海南大学本科生院最新通知为准' }}
        </div>
      </div>

      <!-- 系统日历 .ics 订阅（替代原生锁屏小组件的跨平台通用方案） -->
      <div class="group-label">手机桌面小组件与系统日历同步（iOS / Android / 鸿蒙 / macOS）</div>
      <div class="card padded">
        <div class="row-title strong">一键订阅课表 + 考试 + 作业截止日历 (.ics)</div>
        <div class="row-sub" style="margin-top: 4px">
          订阅后，你的 iPhone 锁屏日程小组件、华为鸿蒙日历卡片、小米/OPPO/vivo 系统日历或电脑 Outlook 会自动展示海大 11 节课表、期末考场与学习通/MOOC 截止提醒。
        </div>

        <template v-if="store.authenticated && store.icsToken">
          <div class="ics-url-box">{{ icsHttpUrl }}</div>
          <div class="btn-grid-2" style="margin-top: 10px">
            <button type="button" class="btn primary" @click="copyIcsUrl">复制订阅链接</button>
            <a :href="icsPathUrl" download="hnutong_schedule.ics" class="btn" style="text-decoration: none">
              下载 .ics 文件
            </a>
          </div>
          <div style="margin-top: 8px; display: flex; justify-content: flex-end">
            <button type="button" class="link-btn" @click="rotateIcsToken">
              链接泄露？重置订阅令牌
            </button>
          </div>
        </template>
        <div v-else style="margin-top: 12px">
          <button type="button" class="btn primary" @click="store.switchTab('me')">绑定教务后生成专属订阅地址</button>
        </div>
      </div>

      <!-- 学年两学期节点时间轴 -->
      <div v-for="term in calendarData?.terms || []" :key="term.id">
        <div class="group-label">{{ term.name }}（{{ term.start }} 起 · {{ term.weeks }} 周）</div>
        <div class="card">
          <div v-for="(mk, idx) in term.marks || []" :key="idx" class="list-row">
            <div>
              <div class="row-title strong">{{ mk.t }}</div>
              <div class="row-sub">
                {{ mk.d }}{{ mk.e ? ` 至 ${mk.e}` : '' }}
              </div>
            </div>
            <div v-if="mk.w" class="row-end">
              <span class="week-pill">{{ mk.w }}</span>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>

  <!-- ==================== 主视图：海大 11 节课表（周视图 / 日视图） ==================== -->
  <div v-else class="schedule-root">
    <!-- 顶栏：周次切换器 + 校历 + 周/日切换 + 同步 -->
    <header class="sched-topbar">
      <div class="week-nav">
        <button type="button" class="icon-btn" aria-label="上一周" @click="stepWeek(-1)">
          <svg viewBox="0 0 24 24"><path d="M15 18l-6-6 6-6" /></svg>
        </button>
        <button type="button" class="week-title-btn" @click="showWeekSheet = true">
          <span class="week-main">第 {{ viewingWeek }} 周</span>
          <span class="week-sub">
            {{ store.isViewingHistoryTerm ? store.activeTimetable?.semesterId : (viewingWeek === store.currentWeekOfToday ? '本周' : `当前第${store.currentWeekOfToday}周`) }}
            ▾
          </span>
        </button>
        <button type="button" class="icon-btn" aria-label="下一周" @click="stepWeek(1)">
          <svg viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
        </button>
        <button
          v-if="viewingWeek !== store.currentWeekOfToday || store.isViewingHistoryTerm"
          type="button"
          class="today-chip"
          @click="backToToday"
        >
          今
        </button>
      </div>

      <div class="sched-actions">
        <button type="button" class="icon-btn" title="海大校历与订阅" @click="store.openSubPage('calendar')">
          校历
        </button>
        <div class="seg-mini">
          <button
            type="button"
            :class="{ active: viewMode === 'week' }"
            @click="viewMode = 'week'"
          >周</button>
          <button
            type="button"
            :class="{ active: viewMode === 'day' }"
            @click="viewMode = 'day'"
          >日</button>
        </div>
        <button
          type="button"
          class="icon-btn"
          :class="{ spin: store.syncing }"
          title="同步教务最新课表"
          @click="handleSyncClick"
        >
          <svg viewBox="0 0 24 24">
            <path d="M21 12a9 9 0 0 0-15.5-6.36L3 8" />
            <path d="M3 3v5h5" />
            <path d="M3 12a9 9 0 0 0 15.5 6.36L21 16" />
            <path d="M16 16h5v5" />
          </svg>
        </button>
      </div>
    </header>

    <!-- 异常提醒条：绝不用空结果覆盖旧课表，而是在顶部温和告知 -->
    <div v-if="store.staleTimetableWarn" class="notice-strip warn">
      <span>上次教务刷新未拿到新课表（{{ store.staleTimetableWarn }}），当前安全展示本地快照</span>
    </div>
    <div v-if="store.isViewingHistoryTerm" class="notice-strip info">
      <span>正在翻阅历史学期：{{ store.activeTimetable?.semester || store.activeTimetable?.semesterId }}</span>
      <button type="button" class="link-btn" @click="exitHistoryTerm">返回本学期</button>
    </div>

    <!-- 未绑定且无本地缓存时的引导页 -->
    <div v-if="!store.authenticated && !store.activeTimetable" class="page-scroll">
      <div class="card padded empty-state">
        <svg viewBox="0 0 24 24">
          <rect x="3" y="4" width="18" height="18" rx="3" />
          <path d="M16 2v4M8 2v4M3 10h18" />
        </svg>
        <h2>海南大学 11 节课表工作台</h2>
        <p>支持海甸/儋州/观澜湖/崖州湾作息预设、课表内直接显示作业截止与期末考场，断网亦可秒开。</p>
        <button type="button" class="btn primary" @click="store.switchTab('me')">
          绑定海南大学教务学号
        </button>
        <div style="display: flex; gap: 16px; margin-top: 6px">
          <button type="button" class="link-btn" @click="store.openSubPage('calendar')">查看 2026–2027 校历</button>
          <button type="button" class="link-btn" @click="store.switchTab('wiki')">浏览海大指南</button>
        </div>
      </div>
    </div>

    <!-- 已加载课表：周网格视图 -->
    <div
      v-else-if="viewMode === 'week'"
      class="week-stage"
      @touchstart.passive="onTouchStart"
      @touchmove.passive="onTouchMove"
      @touchend="onTouchEnd"
      @touchcancel="onTouchCancel"
    >
      <!-- 星期表头 + 当日截止圆点/考试角标 -->
      <div class="week-head-grid">
        <div class="month-cell">{{ weekDates[0]?.monthLabel }}</div>
        <button
          v-for="(wd, dIdx) in weekDates"
          :key="wd.ymd"
          type="button"
          class="day-head-cell"
          :class="{ today: wd.isToday }"
          @click="openDayMarkersOrSwitch(dIdx, wd.ymd)"
        >
          <span class="dh-name">{{ wd.dayName }}</span>
          <span class="dh-date">{{ wd.md }}</span>
          <!-- 课表内嵌截止刻度：作业圆点 / 学习通考试菱形 / 教务考试横带 -->
          <div v-if="dayMarkerSummary(wd.ymd).total > 0" class="dh-markers">
            <span v-if="dayMarkerSummary(wd.ymd).schoolExam > 0" class="mk-exam-band">考</span>
            <span v-if="dayMarkerSummary(wd.ymd).cxExam > 0" class="mk-diamond" title="在线考试截止"></span>
            <span v-if="dayMarkerSummary(wd.ymd).work > 0" class="mk-dot" title="作业/测验截止"></span>
          </div>
        </button>
      </div>

      <!-- 11 节课表滚动网格（跟手横向位移反馈） -->
      <div class="week-body-scroll">
        <div
          class="week-body-inner"
          :style="{ transform: swipeOffsetX ? `translateX(${swipeOffsetX}px)` : undefined }"
        >
          <!-- 左侧 11 节时间栏 -->
          <div class="period-gutter">
            <div
              v-for="p in 11"
              :key="'g' + p"
              class="gutter-cell"
              :class="{ 'break-after': p === 4 || p === 8, 'now-period': currentPeriodIdx === p - 1 }"
            >
              <span class="p-num">{{ p }}</span>
              <span class="p-t1">{{ bellTimeText(p - 1, 0) }}</span>
              <span class="p-t2">{{ bellTimeText(p - 1, 1) }}</span>
            </div>
          </div>

          <!-- 7 天课程列 -->
          <div class="days-columns">
            <div
              v-for="(wd, dayIdx) in weekDates"
              :key="'col-' + wd.ymd"
              class="day-col"
              :class="{ 'today-col': wd.isToday }"
            >
              <!-- 背景节次底格 -->
              <div
                v-for="p in 11"
                :key="'bg-' + p"
                class="bg-slot"
                :class="{ 'break-after': p === 4 || p === 8 }"
              ></div>

              <!-- 连续节次合并后的课程色块 -->
              <button
                v-for="blk in blocksByDay[dayIdx]"
                :key="blk.key"
                type="button"
                class="course-card"
                :class="{
                  'non-cur': !blk.inCurrentWeek,
                  'now-live': wd.isToday && isBlockLiveNow(blk.startPeriod, blk.endPeriod) && blk.inCurrentWeek,
                  'has-conflict': blk.conflictCount > 1,
                }"
                :style="courseBlockStyle(blk)"
                @click="openCourseDetail(blk)"
              >
                <div class="cc-badges">
                  <span v-if="blk.weekParity" class="cc-tag">{{ blk.weekParity }}</span>
                  <span v-if="blk.conflictCount > 1" class="cc-tag warn">撞课×{{ blk.conflictCount }}</span>
                </div>
                <div class="cc-name">
                  {{ blk.inCurrentWeek ? '' : '[非本周] ' }}{{ blk.course.name }}
                </div>
                <div v-if="blk.course.room" class="cc-room">@{{ blk.course.room }}</div>
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>

    <!-- 日视图：时间轴 + 下一节课横幅 + 当日截止清单 -->
    <div v-else class="page-scroll">
      <div class="day-strip">
        <button
          v-for="(wd, idx) in weekDates"
          :key="'ds-' + wd.ymd"
          type="button"
          class="day-pill"
          :class="{ active: selectedDayIdx === idx, today: wd.isToday }"
          @click="selectedDayIdx = idx"
        >
          <span>{{ wd.dayName }}</span>
          <strong>{{ wd.md }}</strong>
        </button>
      </div>

      <!-- 下一节课提醒横幅 -->
      <div v-if="nextClassHint && weekDates[selectedDayIdx]?.isToday" class="card padded next-banner">
        <div class="row-sub" style="color: var(--accent); font-weight: 600">{{ nextClassHint.status }}</div>
        <div class="row-title strong" style="font-size: 17px; margin-top: 2px">{{ nextClassHint.name }}</div>
        <div class="row-sub" style="margin-top: 4px">
          {{ nextClassHint.timeRange }} · {{ nextClassHint.room || '地点待定' }} · {{ nextClassHint.teacher || '任课教师' }}
        </div>
      </div>

      <!-- 当日作业截止与考试卡片 -->
      <template v-if="selectedDayMarkers.length > 0">
        <div class="group-label">当日截止与考试安排（{{ selectedDayMarkers.length }}）</div>
        <div class="card">
          <div v-for="(mk, i) in selectedDayMarkers" :key="i" class="list-row">
            <div>
              <div class="row-title strong">
                <span class="source-pill" :class="mk.markerType">{{ mk.source }}</span>
                {{ mk.title }}
              </div>
              <div class="row-sub">
                {{ mk.course }}
                <template v-if="mk.hhmm"> · {{ mk.hhmm }} 截止</template>
                <template v-if="mk.arrange"> · {{ mk.arrange }} · {{ mk.campus }} {{ mk.room }} (座号 {{ mk.seat || '待定' }})</template>
              </div>
            </div>
          </div>
        </div>
      </template>

      <!-- 当日课程列表 -->
      <div class="group-label">
        {{ weekDates[selectedDayIdx]?.ymd }}（{{ weekDates[selectedDayIdx]?.dayName }}）课程安排
      </div>
      <div v-if="dayViewBlocks.length === 0" class="card padded empty-state">
        <h2>当天没有排课</h2>
        <p>可去「学习」查看待交作业与期中/期末成绩，或点击右上角「校历」查看学期日程。</p>
      </div>
      <div v-else class="card">
        <button
          v-for="blk in dayViewBlocks"
          :key="blk.key"
          type="button"
          class="list-row tap"
          @click="openCourseDetail(blk)"
        >
          <div class="day-time-col">
            <div class="dt-p">第 {{ blk.startPeriod + 1 }}–{{ blk.endPeriod + 1 }} 节</div>
            <div class="dt-hm">{{ bellTimeText(blk.startPeriod, 0) }}–{{ bellTimeText(blk.endPeriod, 1) }}</div>
          </div>
          <div style="flex: 1">
            <div class="row-title strong">{{ blk.course.name }}</div>
            <div class="row-sub">
              {{ blk.course.room || '教室待定' }} · {{ blk.course.teacher || '教师未注' }}
            </div>
          </div>
          <svg class="chev" viewBox="0 0 24 24"><path d="M9 18l6-6-6-6" /></svg>
        </button>
      </div>
    </div>

    <!-- ==================== 弹层 1：周次跳选与历史学期课表切换 ==================== -->
    <div v-if="showWeekSheet" class="sheet-backdrop" @click.self="showWeekSheet = false">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="showWeekSheet = false">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">切换教学周与学期</h3>
        <p class="sheet-sub">
          当前第 {{ store.currentWeekOfToday }} 周 · 开学首周周一 {{ store.preferences.termStartDate }}
        </p>

        <div class="week-picker-grid">
          <button
            v-for="w in totalWeeks"
            :key="'wk-' + w"
            type="button"
            class="wk-cell"
            :class="{ active: viewingWeek === w, current: store.currentWeekOfToday === w }"
            @click="jumpToWeek(w)"
          >
            <span>{{ w }}</span>
            <small v-if="store.currentWeekOfToday === w">本周</small>
          </button>
        </div>

        <div class="field" style="margin-top: 16px">
          <label class="toggle-row">
            <span>显示非本周课程（浅色叠底）</span>
            <input v-model="showNonCurrentWeek" type="checkbox" />
          </label>
        </div>

        <template v-if="store.authenticated && store.semesters.length > 0">
          <div class="group-label" style="padding-left: 0; margin-top: 12px">翻阅历史/其他学期课表</div>
          <div class="inline-chips">
            <button
              type="button"
              class="chip"
              :class="{ active: !store.isViewingHistoryTerm }"
              @click="exitHistoryTerm"
            >
              当前本学期 ({{ store.timetable?.semesterId || '默认' }})
            </button>
            <button
              v-for="sem in store.semesters"
              :key="sem.id"
              type="button"
              class="chip"
              :class="{ active: store.historyTimetable?.semesterId === sem.id }"
              :disabled="termSwitching"
              @click="switchSemesterTimetable(sem.id)"
            >
              {{ sem.id }}
            </button>
          </div>
        </template>
      </div>
    </div>

    <!-- ==================== 弹层 2：课程详情、自定义配色与隐藏管理 ==================== -->
    <div v-if="activeBlock" class="sheet-backdrop" @click.self="activeBlock = null">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="activeBlock = null">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">{{ activeBlock.course.name }}</h3>
        <p class="sheet-sub">
          {{ activeBlock.course.isCustom ? '自定义添加课程' : `课程号 ${activeBlock.course.code || '—'}` }}
        </p>

        <div class="card" style="background: var(--fill); margin-bottom: 14px">
          <div class="list-row">
            <span class="row-sub">上课教室</span>
            <span class="row-title strong">{{ activeBlock.course.room || '未标注' }}</span>
          </div>
          <div class="list-row">
            <span class="row-sub">任课教师</span>
            <span class="row-title">{{ activeBlock.course.teacher || '未标注' }}</span>
          </div>
          <div class="list-row">
            <span class="row-sub">上课节次</span>
            <span class="row-title">
              {{ DAY_NAMES[activeBlock.day] }} 第 {{ activeBlock.startPeriod + 1 }}–{{ activeBlock.endPeriod + 1 }} 节
              ({{ bellTimeText(activeBlock.startPeriod, 0) }}–{{ bellTimeText(activeBlock.endPeriod, 1) }})
            </span>
          </div>
          <div class="list-row">
            <span class="row-sub">教学周次</span>
            <span class="row-title">{{ formatWeeksText(activeBlock.course.weeks) }}</span>
          </div>
        </div>

        <!-- 撞课列表切换 -->
        <div v-if="activeBlock.conflicts.length > 1" style="margin-bottom: 14px">
          <div class="field-label">同一时间段共有 {{ activeBlock.conflicts.length }} 门课程：</div>
          <div class="inline-chips">
            <button
              v-for="(conf, ci) in activeBlock.conflicts"
              :key="ci"
              type="button"
              class="chip"
              :class="{ active: isSameCourse(conf, activeBlock.course) }"
              @click="activeBlock = { ...activeBlock, course: conf, inCurrentWeek: isInWeek(conf, viewingWeek) }"
            >
              {{ conf.name }}
            </button>
          </div>
        </div>

        <!-- 课程专属色卡挑选 -->
        <div class="field">
          <span class="field-label">自定义色块颜色</span>
          <div class="color-swatches">
            <button
              v-for="hex in CUSTOM_SWATCHES"
              :key="hex"
              type="button"
              class="swatch-dot"
              :style="{ background: hex }"
              @click="pickCourseColor(activeBlock.course.name, hex)"
            ></button>
            <button type="button" class="link-btn" @click="pickCourseColor(activeBlock.course.name, '')">
              恢复默认
            </button>
          </div>
        </div>

        <!-- 课程隐藏或删除动作 -->
        <div class="btn-grid-2" style="margin-top: 12px">
          <template v-if="activeBlock.course.isCustom">
            <button
              type="button"
              class="btn"
              style="color: var(--danger); grid-column: span 2"
              @click="deleteCustomCourse(activeBlock.course)"
            >
              删除这门自定义课程
            </button>
          </template>
          <template v-else>
            <button type="button" class="btn" @click="hideCourseSlot(activeBlock)">
              仅隐藏本时段
            </button>
            <button type="button" class="btn" style="color: var(--danger)" @click="hideWholeCourse(activeBlock.course)">
              隐藏整门课
            </button>
          </template>
        </div>
      </div>
    </div>

    <!-- ==================== 弹层 3：点击表头查看某日截止作业与考试 ==================== -->
    <div v-if="dayMarkerSheetYmd" class="sheet-backdrop" @click.self="dayMarkerSheetYmd = ''">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="dayMarkerSheetYmd = ''">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">{{ dayMarkerSheetYmd }} 截止与考试</h3>
        <p class="sheet-sub">汇总自教务考试安排、超星学习通与中国大学 MOOC</p>

        <div class="card" style="background: var(--fill)">
          <div
            v-for="(mk, idx) in store.markersByDate[dayMarkerSheetYmd] || []"
            :key="idx"
            class="list-row"
          >
            <div>
              <div class="row-title strong">
                <span class="source-pill" :class="mk.markerType">{{ mk.source }}</span>
                {{ mk.title }}
              </div>
              <div class="row-sub">
                {{ mk.course }}
                <template v-if="mk.hhmm"> · {{ mk.hhmm }} 截止</template>
                <template v-if="mk.arrange"> · {{ mk.arrange }} · {{ mk.campus }} {{ mk.room }} (座{{ mk.seat || '待定' }})</template>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { apiRequest } from '../api/client';
import { COURSE_PALETTE, useAppStore } from '../stores/app';
import type { CourseItem } from '../types';

const store = useAppStore();

const DAY_NAMES = ['周一', '周二', '周三', '周四', '周五', '周六', '周日'];
const CUSTOM_SWATCHES = ['#2563EB', '#059669', '#D97706', '#DB2777', '#7C3AED', '#0D9488', '#DC2626'];

const viewMode = ref<'week' | 'day'>('week');
const viewingWeek = ref<number>(1);
const selectedDayIdx = ref<number>((new Date().getDay() + 6) % 7);
const showNonCurrentWeek = ref<boolean>(false);
const showWeekSheet = ref<boolean>(false);
const termSwitching = ref<boolean>(false);
const dayMarkerSheetYmd = ref<string>('');

interface RenderedBlock {
  key: string;
  day: number;
  startPeriod: number;
  endPeriod: number;
  course: CourseItem;
  inCurrentWeek: boolean;
  weekParity: string;
  conflictCount: number;
  conflicts: CourseItem[];
}

const activeBlock = ref<RenderedBlock | null>(null);

// 跟手滑动周切换状态
const touchStartX = ref(0);
const touchStartY = ref(0);
const swipeOffsetX = ref(0);
let swipeLockedAxis: '' | 'x' | 'y' = '';

const totalWeeks = computed(() => {
  return store.preferences.totalWeeks || store.activeTimetable?.totalWeeks || 20;
});

watch(
  () => store.currentWeekOfToday,
  (wk) => {
    if (!store.isViewingHistoryTerm) {
      viewingWeek.value = wk;
    }
  },
  { immediate: true }
);

function bellTimeText(periodIdx: number, pos: 0 | 1): string {
  const row = store.activeBellPeriods[periodIdx];
  return row && row[pos] ? row[pos] : '';
}

const calendarData = computed(() => store.meta?.calendarTerms || null);

const icsPathUrl = computed(() =>
  store.icsToken ? `/api/v1/calendar/ics/${store.icsToken}.ics` : ''
);
const icsHttpUrl = computed(() =>
  store.icsToken ? `${window.location.origin}/api/v1/calendar/ics/${store.icsToken}.ics` : ''
);

async function copyIcsUrl() {
  if (!icsHttpUrl.value) return;
  try {
    await navigator.clipboard.writeText(icsHttpUrl.value);
    store.showToast('已复制 .ics 日历订阅链接，可粘贴至手机系统日历「添加订阅日历」');
  } catch {
    store.showToast('复制失败，请手动长按链接复制');
  }
}

async function rotateIcsToken() {
  const res = await apiRequest('/api/v1/calendar/rotate-token', {});
  if (res?.ok && res.icsToken) {
    store.setIcsToken(res.icsToken);
    store.showToast('已生成新的日历订阅令牌，旧链接立即失效');
  } else {
    store.showToast(res?.error || '重置订阅令牌失败，请稍后重试');
  }
}

// 计算当前所看周的周一~周日具体日期（使用年月日构造，彻底规避 DST 夏令时毫秒偏移）
const weekDates = computed(() => {
  const startStr = store.preferences.termStartDate || '2026-08-31';
  const [y, m, d] = startStr.split('-').map(Number);
  const baseY = y || 2026;
  const baseM = (m || 8) - 1;
  const baseD = d || 31;
  const now = new Date();
  const todayYmd = `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;

  return Array.from({ length: 7 }, (_, idx) => {
    const dt = new Date(baseY, baseM, baseD + (viewingWeek.value - 1) * 7 + idx);
    const ymd = `${dt.getFullYear()}-${String(dt.getMonth() + 1).padStart(2, '0')}-${String(dt.getDate()).padStart(2, '0')}`;
    return {
      ymd,
      monthLabel: `${dt.getMonth() + 1}月`,
      md: `${dt.getMonth() + 1}/${dt.getDate()}`,
      dayName: DAY_NAMES[idx],
      isToday: !store.isViewingHistoryTerm && ymd === todayYmd,
    };
  });
});

function isInWeek(c: CourseItem, wk: number): boolean {
  return !c.weeks || c.weeks.length === 0 || c.weeks.includes(wk);
}

function isSameCourse(a: CourseItem, b: CourseItem): boolean {
  return (
    a === b ||
    ((a.code || '') === (b.code || '') &&
      a.name === b.name &&
      (a.teacher || '') === (b.teacher || '') &&
      Boolean(a.isCustom) === Boolean(b.isCustom))
  );
}

function detectParity(weeks: number[]): string {
  if (!weeks || weeks.length < 3) return '';
  const allOdd = weeks.every((w) => w % 2 === 1);
  if (allOdd) return '单';
  const allEven = weeks.every((w) => w % 2 === 0);
  if (allEven) return '双';
  return '';
}

// 按天生成连续节次合并色块，自动检测撞课与本周/非本周优先级
const blocksByDay = computed<RenderedBlock[][]>(() => {
  const wk = viewingWeek.value;
  const days: RenderedBlock[][] = Array.from({ length: 7 }, () => []);

  for (let d = 0; d < 7; d++) {
    // 收集该天每一节 (0..10) 命中的课程
    const slotCourses: CourseItem[][] = Array.from({ length: 11 }, () => []);
    for (const c of store.visibleCourses) {
      const inWk = isInWeek(c, wk);
      if (!inWk && !showNonCurrentWeek.value) continue;
      for (const s of c.slots || []) {
        if (s.day === d && s.period >= 0 && s.period < 11) {
          slotCourses[s.period].push(c);
        }
      }
    }

    let p = 0;
    while (p < 11) {
      const list = slotCourses[p];
      if (list.length === 0) {
        p++;
        continue;
      }
      // 本周有课的优先展示在顶层（兼容 weeks 为空的全学期课程）
      const sorted = [...list].sort((a, b) => {
        const aIn = isInWeek(a, wk) ? 1 : 0;
        const bIn = isInWeek(b, wk) ? 1 : 0;
        return bIn - aIn;
      });
      const primary = sorted[0];
      const sig = sorted.map((c) => `${c.code || ''}|${c.name}`).join('||');

      let endP = p;
      while (endP + 1 < 11) {
        const nextList = slotCourses[endP + 1];
        const nextSig = [...nextList]
          .sort((a, b) => (isInWeek(b, wk) ? 1 : 0) - (isInWeek(a, wk) ? 1 : 0))
          .map((c) => `${c.code || ''}|${c.name}`)
          .join('||');
        if (nextSig === sig) endP++;
        else break;
      }

      const inCurrent = isInWeek(primary, wk);
      days[d].push({
        key: `${d}-${p}-${endP}-${primary.code}-${primary.name}`,
        day: d,
        startPeriod: p,
        endPeriod: endP,
        course: primary,
        inCurrentWeek: inCurrent,
        weekParity: detectParity(primary.weeks || []),
        conflictCount: sorted.filter((x) => isInWeek(x, wk)).length,
        conflicts: sorted,
      });
      p = endP + 1;
    }
  }
  return days;
});

const dayViewBlocks = computed(() => {
  return blocksByDay.value[selectedDayIdx.value]?.filter((b) => b.inCurrentWeek) || [];
});

const selectedDayMarkers = computed(() => {
  const ymd = weekDates.value[selectedDayIdx.value]?.ymd || '';
  return store.markersByDate[ymd] || [];
});

function dayMarkerSummary(ymd: string) {
  const list = store.markersByDate[ymd] || [];
  let work = 0;
  let cxExam = 0;
  let schoolExam = 0;
  for (const m of list) {
    if (m.markerType === 'school_exam') schoolExam++;
    else if (m.markerType === 'cx_exam') cxExam++;
    else work++;
  }
  return { total: list.length, work, cxExam, schoolExam };
}

function openDayMarkersOrSwitch(dayIdx: number, ymd: string) {
  selectedDayIdx.value = dayIdx;
  if ((store.markersByDate[ymd] || []).length > 0) {
    dayMarkerSheetYmd.value = ymd;
  } else {
    viewMode.value = 'day';
  }
}

// 实时计算当前正在上第几节课 & 响应系统深浅色切换
const nowMinutes = ref<number>(new Date().getHours() * 60 + new Date().getMinutes());
const isDarkTheme = ref<boolean>(
  typeof window !== 'undefined' && Boolean(window.matchMedia?.('(prefers-color-scheme: dark)').matches)
);
let nowTimer: number | undefined;
let darkMql: MediaQueryList | null = null;
const onDarkChange = (e: MediaQueryListEvent) => {
  isDarkTheme.value = e.matches;
};

onMounted(() => {
  nowTimer = window.setInterval(() => {
    const d = new Date();
    nowMinutes.value = d.getHours() * 60 + d.getMinutes();
  }, 30000);
  if (window.matchMedia) {
    darkMql = window.matchMedia('(prefers-color-scheme: dark)');
    isDarkTheme.value = darkMql.matches;
    darkMql.addEventListener?.('change', onDarkChange);
  }
});

onUnmounted(() => {
  if (nowTimer) {
    window.clearInterval(nowTimer);
    nowTimer = undefined;
  }
  darkMql?.removeEventListener?.('change', onDarkChange);
});

function parseHm(hm: string): number {
  const [h, m] = (hm || '00:00').split(':').map(Number);
  return (h || 0) * 60 + (m || 0);
}

const currentPeriodIdx = computed<number>(() => {
  const m = nowMinutes.value;
  for (let i = 0; i < store.activeBellPeriods.length; i++) {
    const s = parseHm(store.activeBellPeriods[i][0]);
    const e = parseHm(store.activeBellPeriods[i][1]);
    if (m >= s && m <= e) return i;
  }
  return -1;
});

function isBlockLiveNow(startP: number, endP: number): boolean {
  const s = parseHm(bellTimeText(startP, 0));
  const e = parseHm(bellTimeText(endP, 1));
  return nowMinutes.value >= s && nowMinutes.value <= e;
}

const nextClassHint = computed(() => {
  const blocks = dayViewBlocks.value;
  const nowM = nowMinutes.value;
  for (const b of blocks) {
    const s = parseHm(bellTimeText(b.startPeriod, 0));
    const e = parseHm(bellTimeText(b.endPeriod, 1));
    if (nowM >= s && nowM <= e) {
      return {
        status: `正在上课 · 还剩 ${e - nowM} 分钟下课`,
        name: b.course.name,
        timeRange: `${bellTimeText(b.startPeriod, 0)}–${bellTimeText(b.endPeriod, 1)}`,
        room: b.course.room,
        teacher: b.course.teacher,
      };
    }
    if (s > nowM) {
      const diff = s - nowM;
      const hrs = Math.floor(diff / 60);
      const mins = diff % 60;
      return {
        status: `下一节课 · ${hrs > 0 ? `${hrs}小时` : ''}${mins}分钟后开始`,
        name: b.course.name,
        timeRange: `${bellTimeText(b.startPeriod, 0)}–${bellTimeText(b.endPeriod, 1)}`,
        room: b.course.room,
        teacher: b.course.teacher,
      };
    }
  }
  return null;
});

function courseBlockStyle(blk: RenderedBlock) {
  const ROW_H = 54;
  const topPx = blk.startPeriod * ROW_H + (blk.startPeriod >= 4 ? 6 : 0) + (blk.startPeriod >= 8 ? 6 : 0);
  let extraBreak = 0;
  if (blk.startPeriod < 4 && blk.endPeriod >= 4) extraBreak += 6;
  if (blk.startPeriod < 8 && blk.endPeriod >= 8) extraBreak += 6;
  const heightPx = (blk.endPeriod - blk.startPeriod + 1) * ROW_H + extraBreak - 3;

  const isDark = isDarkTheme.value;
  const pal = store.courseColorMap[blk.course.name] || COURSE_PALETTE[0];
  return {
    top: `${topPx + 1}px`,
    height: `${heightPx}px`,
    backgroundColor: isDark ? pal.darkBg : pal.bg,
    color: isDark ? pal.darkFg : pal.fg,
  };
}

function stepWeek(delta: number) {
  const next = viewingWeek.value + delta;
  if (next >= 1 && next <= totalWeeks.value) {
    viewingWeek.value = next;
  }
}

function jumpToWeek(w: number) {
  viewingWeek.value = w;
  showWeekSheet.value = false;
}

function backToToday() {
  store.historyTimetable = null;
  viewingWeek.value = store.currentWeekOfToday;
  selectedDayIdx.value = (new Date().getDay() + 6) % 7;
}

async function handleSyncClick() {
  if (!store.authenticated) {
    store.showToast('请先绑定海南大学教务账号');
    store.switchTab('me');
    return;
  }
  await store.syncJwxt();
}

async function switchSemesterTimetable(semId: string) {
  if (semId === store.timetable?.semesterId) {
    exitHistoryTerm();
    return;
  }
  termSwitching.value = true;
  try {
    const res = await store.runTrackedTask(
      {
        title: `正在切换 ${semId} 课表`,
        stage: '从海南大学教务拉取历史学期课表…',
        kind: 'sync',
        doneStage: `已加载 ${semId} 课表`,
      },
      () => apiRequest('/api/v1/jw/timetable', { semesterId: semId })
    );
    if (res?.ok && Array.isArray(res.courses)) {
      store.historyTimetable = {
        semesterId: res.semesterId || semId,
        semester: res.semester || semId,
        unitCount: res.unitCount || 11,
        totalWeeks: res.totalWeeks || 20,
        courses: res.courses,
      };
      viewingWeek.value = 1;
      showWeekSheet.value = false;
      store.showToast(`已切换查看 ${semId} 课表`);
    } else {
      store.showToast(res?.error || '获取历史学期课表失败');
    }
  } finally {
    termSwitching.value = false;
  }
}

function exitHistoryTerm() {
  store.historyTimetable = null;
  viewingWeek.value = store.currentWeekOfToday;
  showWeekSheet.value = false;
}

// 手势左右划动换周
function onTouchStart(e: TouchEvent) {
  if (e.touches.length !== 1) return;
  touchStartX.value = e.touches[0].clientX;
  touchStartY.value = e.touches[0].clientY;
  swipeOffsetX.value = 0;
  swipeLockedAxis = '';
}

function onTouchMove(e: TouchEvent) {
  if (e.touches.length !== 1) return;
  const dx = e.touches[0].clientX - touchStartX.value;
  const dy = e.touches[0].clientY - touchStartY.value;
  if (!swipeLockedAxis) {
    if (Math.abs(dx) > 10 || Math.abs(dy) > 10) {
      swipeLockedAxis = Math.abs(dx) > Math.abs(dy) * 1.2 ? 'x' : 'y';
    }
  }
  if (swipeLockedAxis === 'x') {
    swipeOffsetX.value = Math.max(-90, Math.min(90, dx * 0.45));
  }
}

function onTouchEnd() {
  if (swipeLockedAxis === 'x') {
    if (swipeOffsetX.value < -32) stepWeek(1);
    else if (swipeOffsetX.value > 32) stepWeek(-1);
  }
  swipeOffsetX.value = 0;
  swipeLockedAxis = '';
}

function onTouchCancel() {
  swipeOffsetX.value = 0;
  swipeLockedAxis = '';
}

function openCourseDetail(blk: RenderedBlock) {
  activeBlock.value = blk;
}

function formatWeeksText(weeks: number[]): string {
  if (!weeks || weeks.length === 0) return '全学期';
  const sorted = [...weeks].sort((a, b) => a - b);
  return `第 ${sorted[0]}–${sorted[sorted.length - 1]} 周（共 ${sorted.length} 周）`;
}

async function pickCourseColor(courseName: string, hex: string) {
  const nextColors = { ...(store.preferences.courseColors || {}) };
  if (hex) nextColors[courseName] = hex;
  else delete nextColors[courseName];
  await store.updatePreferences({ courseColors: nextColors });
}

async function hideCourseSlot(blk: RenderedBlock) {
  const nextSlots = [...(store.preferences.hiddenSlots || [])];
  for (let p = blk.startPeriod; p <= blk.endPeriod; p++) {
    nextSlots.push({
      code: blk.course.code,
      name: blk.course.name,
      day: blk.day,
      period: p,
    });
  }
  await store.updatePreferences({ hiddenSlots: nextSlots });
  activeBlock.value = null;
  store.showToast('已隐藏该时段（可在「我的 → 课程调整」恢复）');
}

async function hideWholeCourse(course: CourseItem) {
  const hideKey = course.code || `NAME:${course.name}`;
  if (!hideKey || hideKey === 'NAME:') return;
  const nextHidden = Array.from(new Set([...(store.preferences.hiddenCourses || []), hideKey]));
  await store.updatePreferences({ hiddenCourses: nextHidden });
  activeBlock.value = null;
  store.showToast(`已隐藏《${course.name}》（可在「我的 → 课程调整」恢复）`);
}

async function deleteCustomCourse(course: CourseItem) {
  const nextCustom = (store.preferences.customCourses || []).filter(
    (c) => !(c.name === course.name && c.code === course.code)
  );
  await store.updatePreferences({ customCourses: nextCustom });
  activeBlock.value = null;
  store.showToast('已删除自定义课程');
}
</script>

<style scoped>
.schedule-root,
.subpage-wrap {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}

.sched-topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 6px;
  padding: 8px 12px 6px;
  flex-shrink: 0;
  flex-wrap: wrap;
}

.week-nav {
  display: flex;
  align-items: center;
  gap: 4px;
}

.week-title-btn {
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  padding: 2px 8px;
  border-radius: var(--r-list);
}

.week-title-btn:active {
  background: var(--fill);
}

.week-main {
  font-size: 17px;
  font-weight: 700;
  line-height: 1.15;
}

.week-sub {
  font-size: 11px;
  color: var(--sub);
}

.today-chip {
  min-width: 28px;
  height: 28px;
  padding: 0 8px;
  border-radius: 999px;
  background: var(--accent);
  color: #fff;
  font-size: 12px;
  font-weight: 700;
}

.sched-actions {
  display: flex;
  align-items: center;
  gap: 6px;
}

.seg-mini {
  display: inline-flex;
  background: var(--fill);
  border-radius: 999px;
  padding: 2px;
}

.seg-mini button {
  min-width: 30px;
  height: 30px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 600;
  color: var(--sub);
}

.seg-mini button.active {
  background: var(--card);
  color: var(--text);
}

.notice-strip {
  margin: 0 12px 6px;
  padding: 8px 12px;
  border-radius: var(--r-list);
  font-size: 12px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.notice-strip.warn {
  background: var(--warn-bg);
  color: var(--warn);
}

.notice-strip.info {
  background: var(--accent-soft);
  color: var(--accent);
}

/* 周视图网格 */
.week-stage {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  margin: 0 6px;
  background: var(--card);
  border-radius: var(--r-card);
  overflow: hidden;
}

.week-head-grid {
  display: grid;
  grid-template-columns: 42px repeat(7, minmax(0, 1fr));
  border-bottom: 0.5px solid var(--sep);
  flex-shrink: 0;
}

.month-cell {
  font-size: 11px;
  font-weight: 700;
  color: var(--sub);
  display: flex;
  align-items: center;
  justify-content: center;
}

.day-head-cell {
  padding: 6px 2px 5px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 1px;
  position: relative;
}

.day-head-cell.today {
  background: var(--accent-soft);
}

.day-head-cell.today .dh-name,
.day-head-cell.today .dh-date {
  color: var(--accent);
  font-weight: 700;
}

.dh-name {
  font-size: 12px;
  font-weight: 600;
}

.dh-date {
  font-size: 10px;
  color: var(--sub);
}

.dh-markers {
  display: flex;
  align-items: center;
  gap: 3px;
  margin-top: 1px;
}

.mk-dot {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--due-work);
}

.mk-diamond {
  width: 6px;
  height: 6px;
  background: var(--due-exam);
  transform: rotate(45deg);
}

.mk-exam-band {
  font-size: 9px;
  line-height: 1;
  padding: 1px 3px;
  border-radius: 3px;
  background: var(--due-exam-bg);
  color: var(--due-exam);
  font-weight: 700;
}

.week-body-scroll {
  flex: 1;
  overflow-y: auto;
  overflow-x: hidden;
  padding-bottom: calc(68px + env(safe-area-inset-bottom, 0px));
}

@media (min-width: 860px) {
  .week-body-scroll {
    padding-bottom: 12px;
  }
}

.week-body-inner {
  display: grid;
  grid-template-columns: 42px 1fr;
  transition: transform 140ms ease-out;
}

.period-gutter {
  border-right: 0.5px solid var(--sep);
}

.gutter-cell {
  height: 54px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  border-bottom: 0.5px solid var(--sep);
}

.gutter-cell.break-after {
  margin-bottom: 6px;
}

.gutter-cell.now-period {
  background: var(--accent-soft);
}

.p-num {
  font-size: 12px;
  font-weight: 700;
}

.p-t1,
.p-t2 {
  font-size: 8.5px;
  color: var(--sub);
  line-height: 1.15;
}

.days-columns {
  display: grid;
  grid-template-columns: repeat(7, minmax(0, 1fr));
}

.day-col {
  position: relative;
  border-right: 0.5px solid var(--sep);
}

.day-col:last-child {
  border-right: none;
}

.day-col.today-col {
  background: rgba(0, 122, 255, 0.03);
}

.bg-slot {
  height: 54px;
  border-bottom: 0.5px solid var(--sep);
}

.bg-slot.break-after {
  margin-bottom: 6px;
}

.course-card {
  position: absolute;
  left: 1.5px;
  right: 1.5px;
  border-radius: 7px;
  padding: 3px 4px;
  display: flex;
  flex-direction: column;
  justify-content: flex-start;
  align-items: flex-start;
  text-align: left;
  overflow: hidden;
  z-index: 2;
}

.course-card.non-cur {
  opacity: 0.45;
  filter: grayscale(0.4);
}

.course-card.now-live {
  box-shadow: inset 0 0 0 2px var(--accent);
}

.course-card.has-conflict {
  border: 1px dashed currentColor;
}

.cc-badges {
  display: flex;
  gap: 2px;
  margin-bottom: 1px;
}

.cc-tag {
  font-size: 8.5px;
  font-weight: 700;
  padding: 0 3px;
  border-radius: 3px;
  background: rgba(0, 0, 0, 0.08);
}

.cc-name {
  font-size: 11px;
  font-weight: 700;
  line-height: 1.22;
  display: -webkit-box;
  -webkit-line-clamp: 3;
  -webkit-box-orient: vertical;
  overflow: hidden;
  word-break: break-all;
}

.cc-room {
  margin-top: auto;
  font-size: 9.5px;
  line-height: 1.15;
  opacity: 0.88;
  word-break: break-all;
}

/* 日视图 */
.day-strip {
  display: grid;
  grid-template-columns: repeat(7, 1fr);
  gap: 6px;
  margin-bottom: 12px;
}

.day-pill {
  padding: 8px 4px;
  border-radius: var(--r-list);
  background: var(--card);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  font-size: 11px;
  color: var(--sub);
}

.day-pill strong {
  font-size: 13px;
  color: var(--text);
}

.day-pill.active {
  background: var(--accent);
  color: #fff;
}

.day-pill.active strong {
  color: #fff;
}

.next-banner {
  border-left: 4px solid var(--accent);
}

.day-time-col {
  width: 96px;
  flex-shrink: 0;
}

.dt-p {
  font-size: 13px;
  font-weight: 700;
  color: var(--accent);
}

.dt-hm {
  font-size: 11px;
  color: var(--sub);
}

.source-pill {
  display: inline-block;
  padding: 1px 6px;
  border-radius: 4px;
  font-size: 11px;
  margin-right: 6px;
  background: var(--fill);
}

.source-pill.work_due {
  color: var(--due-work);
}

.source-pill.cx_exam,
.source-pill.school_exam {
  background: var(--due-exam-bg);
  color: var(--due-exam);
}

/* 弹层与通用辅助 */
.week-picker-grid {
  display: grid;
  grid-template-columns: repeat(5, 1fr);
  gap: 8px;
}

.wk-cell {
  height: 44px;
  border-radius: var(--r-list);
  background: var(--fill);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  font-size: 14px;
  font-weight: 600;
}

.wk-cell small {
  font-size: 9px;
  color: var(--accent);
}

.wk-cell.active {
  background: var(--accent);
  color: #fff;
}

.wk-cell.active small {
  color: #fff;
}

.toggle-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 14px;
  cursor: pointer;
}

.inline-chips {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}

.week-pill {
  padding: 2px 8px;
  border-radius: 6px;
  background: var(--fill);
  font-size: 12px;
  font-weight: 600;
}

.cal-hero-top {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
}

.cal-big-week {
  font-size: 26px;
  font-weight: 700;
  margin-top: 4px;
}

.cal-sub-total {
  font-size: 14px;
  font-weight: 400;
  color: var(--sub);
}

.progress-track {
  height: 8px;
  border-radius: 999px;
  background: var(--fill);
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: var(--accent);
  border-radius: 999px;
}

.ics-url-box {
  margin-top: 10px;
  padding: 10px 12px;
  border-radius: var(--r-list);
  background: var(--fill);
  font-size: 12px;
  word-break: break-all;
  color: var(--sub);
}

.btn-grid-2 {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}

.color-swatches {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}

.swatch-dot {
  width: 28px;
  height: 28px;
  border-radius: 50%;
}
</style>
