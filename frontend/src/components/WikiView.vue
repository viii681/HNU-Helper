<template>
  <div class="wiki-root">
    <header class="page-header">
      <div>
        <h1>{{ wikiData?.title || '海大指南' }}</h1>
      </div>
    </header>

    <div class="page-scroll">
      <!-- 搜索框 -->
      <div class="search-bar-wrap">
        <input
          v-model.trim="keyword"
          type="search"
          class="input"
          placeholder="搜索校区、书院、防潮、台风、选课、小电驴…"
        />
      </div>

      <!-- 海大五校区与全部十七书院双院制全景速览卡片 -->
      <div class="card padded">
        <div class="flex-between">
          <div class="row-title strong">海南大学「五校区 · 十七书院」双院制全景</div>
          <span class="ac-count-badge">共 {{ store.allAcademiesList.length }} 书院</span>
        </div>
        <div class="row-sub" style="margin-top: 4px">
          专业培养归口各学院教务办，住宿社区与日常思政归口各书院。早第一节课为 07:40，跨片区上课请预留骑行时间。
        </div>

        <div class="campus-badges">
          <button
            type="button"
            class="c-pill"
            :class="{ active: selectedCampusId === '' }"
            @click="selectedCampusId = ''"
          >
            全部校区 ({{ store.allAcademiesList.length }})
          </button>
          <button
            v-for="grp in store.campusAcademiesList"
            :key="grp.campusId"
            type="button"
            class="c-pill"
            :class="{ active: selectedCampusId === grp.campusId }"
            @click="selectedCampusId = grp.campusId"
          >
            {{ grp.campusName }} ({{ grp.academies.length }})
          </button>
        </div>

        <div v-for="grp in displayedCampusAcademies" :key="'g-' + grp.campusId" class="wiki-ac-group">
          <div class="wiki-ac-head">{{ grp.campusName }}</div>
          <div class="wiki-ac-pills">
            <span
              v-for="ac in grp.academies"
              :key="ac"
              class="wiki-ac-pill"
              :class="{ mine: store.profile?.academy === ac }"
            >
              {{ ac }}<template v-if="store.profile?.academy === ac"> · 我的</template>
            </span>
          </div>
        </div>
      </div>

      <!-- 分组双列百科卡片 -->
      <div v-if="filteredGroups.length === 0" class="card padded empty-state">
        <h2>没有匹配的指南词条</h2>
        <p>换个关键词试试，例如「台风」「防潮」「选课」「飞跃手册」。</p>
      </div>

      <div v-for="grp in filteredGroups" :key="grp.group" class="wiki-section">
        <div class="group-label">{{ grp.group }}</div>
        <div class="wiki-grid">
          <button
            v-for="item in grp.items"
            :key="item.title"
            type="button"
            class="card padded wiki-card"
            @click="activeItem = item"
          >
            <div class="wiki-card-top">
              <span class="row-title strong">{{ item.title }}</span>
              <span v-if="item.url" class="ext-mark">↗</span>
            </div>
            <div class="wiki-excerpt">{{ item.summary }}</div>
          </button>
        </div>
      </div>
    </div>

    <!-- 指南阅读详情抽屉 -->
    <div v-if="activeItem" class="sheet-backdrop" @click.self="activeItem = null">
      <div class="sheet-panel">
        <div class="sheet-grab"></div>
        <button type="button" class="sheet-close" @click="activeItem = null">
          <svg viewBox="0 0 24 24"><path d="M18 6L6 18M6 6l12 12" /></svg>
        </button>
        <h3 class="sheet-title">{{ activeItem.title }}</h3>
        <p class="sheet-sub">海南大学校园生活与学业实用指南</p>

        <div class="wiki-detail-body">{{ activeItem.summary }}</div>

        <div v-if="activeItem.url" style="margin-top: 18px">
          <a
            :href="activeItem.url"
            target="_blank"
            rel="noopener noreferrer"
            class="btn primary block"
            style="text-decoration: none"
          >
            打开对应站点 / 开源仓库 ↗
          </a>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, ref } from 'vue';
import { useAppStore } from '../stores/app';

const store = useAppStore();
const keyword = ref('');
const selectedCampusId = ref('');
const activeItem = ref<any>(null);

const wikiData = computed(() => store.meta?.wiki || null);

const displayedCampusAcademies = computed(() => {
  const kw = keyword.value.toLowerCase();
  return store.campusAcademiesList
    .filter((g) => !selectedCampusId.value || g.campusId === selectedCampusId.value)
    .map((g) => ({
      ...g,
      academies: kw
        ? g.academies.filter(
            (ac) => ac.toLowerCase().includes(kw) || g.campusName.toLowerCase().includes(kw)
          )
        : g.academies,
    }))
    .filter((g) => g.academies.length > 0);
});

const ALLOWED_LINK_GROUP = '学业进阶 · 选课与共享攻略';

const filteredGroups = computed(() => {
  const groups = (wikiData.value?.groups || []).map((g: any) => ({
    group: g.group,
    items: (g.items || []).map((it: any) => ({
      ...it,
      url: g.group === ALLOWED_LINK_GROUP ? (it.url || '') : '',
    })),
  }));
  const kw = keyword.value.toLowerCase();
  if (!kw) return groups;
  return groups
    .map((g: any) => ({
      group: g.group,
      items: (g.items || []).filter(
        (it: any) =>
          String(it.title || '').toLowerCase().includes(kw) ||
          String(it.summary || '').toLowerCase().includes(kw) ||
          String(g.group || '').toLowerCase().includes(kw)
      ),
    }))
    .filter((g: any) => g.items.length > 0);
});
</script>

<style scoped>
.wiki-root {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}

.search-bar-wrap {
  margin-bottom: 12px;
}

.flex-between {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
}

.ac-count-badge {
  padding: 2px 8px;
  border-radius: 999px;
  background: var(--accent-soft);
  color: var(--accent);
  font-size: 11px;
  font-weight: 700;
  flex-shrink: 0;
}

.campus-badges {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 10px;
}

.c-pill {
  padding: 5px 10px;
  border-radius: var(--r-chip);
  background: var(--fill);
  color: var(--sub);
  font-size: 12px;
  font-weight: 600;
}

.c-pill.active {
  background: var(--accent);
  color: #fff;
}

.wiki-ac-group {
  margin-top: 10px;
  padding-top: 8px;
  border-top: 0.5px solid var(--sep);
}

.wiki-ac-head {
  font-size: 12px;
  font-weight: 600;
  color: var(--sub);
  margin-bottom: 6px;
}

.wiki-ac-pills {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.wiki-ac-pill {
  padding: 3px 9px;
  border-radius: 999px;
  background: var(--fill);
  color: var(--text);
  font-size: 12px;
  font-weight: 500;
}

.wiki-ac-pill.mine {
  background: var(--accent-soft);
  color: var(--accent);
  font-weight: 700;
}

.wiki-section {
  margin-bottom: 8px;
}

.wiki-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 10px;
}

@media (max-width: 480px) {
  .wiki-grid {
    grid-template-columns: 1fr;
  }
}

.wiki-card {
  margin-bottom: 0;
  text-align: left;
  display: flex;
  flex-direction: column;
  gap: 6px;
  transition: transform 0.12s;
}

.wiki-card:active {
  transform: scale(0.985);
}

.wiki-card-top {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 8px;
}

.ext-mark {
  font-size: 13px;
  color: var(--accent);
  flex-shrink: 0;
}

.wiki-excerpt {
  font-size: 13px;
  color: var(--sub);
  line-height: 1.45;
  display: -webkit-box;
  -webkit-line-clamp: 3;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

.wiki-detail-body {
  font-size: 15px;
  line-height: 1.65;
  color: var(--text);
  background: var(--fill);
  padding: 14px 16px;
  border-radius: var(--r-card);
  white-space: pre-wrap;
}
</style>
