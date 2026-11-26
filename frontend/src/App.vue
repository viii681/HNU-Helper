<template>
  <div class="app-shell">
    <!-- 桌面宽屏左侧导航栏（≥860px 自动切换为双栏工作台） -->
    <aside class="side-rail">
      <div class="brand-box">
        <h2>海大通</h2>
        <p>海南大学校园一体化平台</p>
      </div>

      <button
        v-for="item in NAV_ITEMS"
        :key="item.key"
        type="button"
        class="rail-btn"
        :class="{ active: store.activeTab === item.key }"
        @click="store.switchTab(item.key)"
      >
        <svg viewBox="0 0 24 24" v-html="item.svg"></svg>
        <span>{{ item.label }}</span>
      </button>
    </aside>

    <!-- 主内容舞台 -->
    <main class="main-stage">
      <ScheduleView v-if="store.activeTab === 'schedule'" />
      <StudyView v-else-if="store.activeTab === 'study'" />
      <WikiView v-else-if="store.activeTab === 'wiki'" />
      <CampusView v-else-if="store.activeTab === 'campus'" />
      <MeView v-else />
    </main>

    <!-- 手机端底部五格毛玻璃导航栏 -->
    <nav class="bottom-tabbar" aria-label="主导航">
      <button
        v-for="item in NAV_ITEMS"
        :key="'m-' + item.key"
        type="button"
        class="tab-item"
        :class="{ active: store.activeTab === item.key }"
        @click="store.switchTab(item.key)"
      >
        <svg viewBox="0 0 24 24" v-html="item.svg"></svg>
        <span>{{ item.label }}</span>
      </button>
    </nav>

    <!-- 全局实时数据同步/上传更新动画进度浮窗 -->
    <SyncProgressBar variant="floating" />

    <!-- 全局轻量胶囊提示 -->
    <div v-if="store.toastMsg" class="toast-pill" role="status">
      {{ store.toastMsg }}
    </div>
  </div>
</template>

<script setup lang="ts">
import { onMounted } from 'vue';
import CampusView from './components/CampusView.vue';
import MeView from './components/MeView.vue';
import ScheduleView from './components/ScheduleView.vue';
import StudyView from './components/StudyView.vue';
import SyncProgressBar from './components/SyncProgressBar.vue';
import WikiView from './components/WikiView.vue';
import { useAppStore } from './stores/app';
import type { MainTab } from './types';

const store = useAppStore();

const NAV_ITEMS: Array<{ key: MainTab; label: string; svg: string }> = [
  {
    key: 'schedule',
    label: '课表',
    svg: '<rect x="3" y="4" width="18" height="18" rx="2.5"/><line x1="16" y1="2" x2="16" y2="6"/><line x1="8" y1="2" x2="8" y2="6"/><line x1="3" y1="10" x2="21" y2="10"/>',
  },
  {
    key: 'study',
    label: '学习',
    svg: '<path d="M2 3h6a4 4 0 0 1 4 4v14a3 3 0 0 0-3-3H2z"/><path d="M22 3h-6a4 4 0 0 0-4 4v14a3 3 0 0 1 3-3h7z"/>',
  },
  {
    key: 'wiki',
    label: '海大指南',
    svg: '<circle cx="12" cy="12" r="10"/><polygon points="16.24 7.76 14.12 14.12 7.76 16.24 9.88 9.88 16.24 7.76"/>',
  },
  {
    key: 'campus',
    label: '校园',
    svg: '<path d="M3 21h18"/><path d="M5 21V7l7-4 7 4v14"/><path d="M9 9h1M14 9h1M9 13h1M14 13h1M10 21v-4h4v4"/>',
  },
  {
    key: 'me',
    label: '我的',
    svg: '<path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>',
  },
];

onMounted(() => {
  store.initApp();
});
</script>
