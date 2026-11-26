<template>
  <transition name="hnu-prog-fade">
    <div
      v-if="displayTask && displayTask.active"
      class="hnu-prog-wrap"
      :class="[`variant-${variant}`, `status-${displayTask.status}`]"
      role="progressbar"
      :aria-valuenow="Math.round(displayTask.percent)"
      aria-valuemin="0"
      aria-valuemax="100"
    >
      <div class="hnu-prog-head">
        <div class="hnu-prog-title-row">
          <span class="hnu-prog-dot" :class="displayTask.status"></span>
          <span class="hnu-prog-title">{{ displayTask.title }}</span>
          <span v-if="displayTask.totalSteps > 1 && displayTask.status === 'running'" class="hnu-prog-step">
            第 {{ Math.max(1, displayTask.step) }}/{{ displayTask.totalSteps }} 步
          </span>
        </div>
        <div class="hnu-prog-metrics">
          <span class="hnu-prog-elapsed">{{ formattedElapsed }}</span>
          <span class="hnu-prog-pct">{{ Math.round(displayTask.percent) }}%</span>
        </div>
      </div>

      <div class="hnu-prog-track">
        <div
          class="hnu-prog-fill"
          :class="displayTask.status"
          :style="{ width: Math.max(4, Math.min(100, displayTask.percent)) + '%' }"
        >
          <span v-if="displayTask.status === 'running'" class="hnu-prog-shimmer"></span>
          <span v-if="displayTask.status === 'running'" class="hnu-prog-tip-glow"></span>
        </div>
      </div>

      <div class="hnu-prog-foot">
        <span class="hnu-prog-stage">{{ displayTask.stage }}</span>
        <span v-if="displayTask.detail" class="hnu-prog-detail">{{ displayTask.detail }}</span>
      </div>

      <div v-if="displayTask.totalSteps > 1 && displayTask.totalSteps <= 8" class="hnu-prog-pips">
        <span
          v-for="idx in displayTask.totalSteps"
          :key="idx"
          class="hnu-prog-pip"
          :class="{
            done: displayTask.status === 'done' || idx < displayTask.step,
            active: displayTask.status === 'running' && idx === Math.max(1, displayTask.step),
            err: displayTask.status === 'error' && idx === Math.max(1, displayTask.step),
          }"
        ></span>
      </div>
    </div>
  </transition>
</template>

<script setup lang="ts">
import { computed } from 'vue';
import { useAppStore } from '../stores/app';
import type { ProgressTaskState } from '../types';

const props = withDefaults(
  defineProps<{
    variant?: 'floating' | 'inline';
    task?: ProgressTaskState | null;
  }>(),
  {
    variant: 'floating',
    task: null,
  }
);

const store = useAppStore();
const displayTask = computed(() => props.task || store.progressTask);

const formattedElapsed = computed(() => {
  const sec = displayTask.value?.elapsedSec ?? 0;
  return `${sec.toFixed(1)}s`;
});
</script>

<style scoped>
.hnu-prog-wrap {
  border-radius: var(--r-card);
  padding: 12px 14px;
  color: var(--text);
  transition: border-color 0.2s ease, background-color 0.2s ease;
}

.hnu-prog-wrap.variant-floating {
  position: fixed;
  top: 12px;
  left: 50%;
  transform: translateX(-50%);
  z-index: 92;
  width: min(460px, calc(100vw - 24px));
  background: rgba(255, 255, 255, 0.92);
  -webkit-backdrop-filter: saturate(180%) blur(20px);
  backdrop-filter: saturate(180%) blur(20px);
  box-shadow: 0 10px 32px rgba(0, 0, 0, 0.14), 0 0 0 0.5px var(--sep);
}

@media (prefers-color-scheme: dark) {
  .hnu-prog-wrap.variant-floating {
    background: rgba(28, 28, 30, 0.92);
    box-shadow: 0 12px 36px rgba(0, 0, 0, 0.48), 0 0 0 0.5px rgba(255, 255, 255, 0.14);
  }
}

.hnu-prog-wrap.variant-inline {
  background: var(--fill);
  margin: 10px 0;
}

.hnu-prog-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  margin-bottom: 8px;
}

.hnu-prog-title-row {
  display: flex;
  align-items: center;
  gap: 7px;
  min-width: 0;
}

.hnu-prog-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--accent);
  flex-shrink: 0;
}

.hnu-prog-dot.running {
  animation: hnu-pulse-dot 1.15s ease-in-out infinite;
}

.hnu-prog-dot.done {
  background: var(--ok);
}

.hnu-prog-dot.error {
  background: var(--danger);
}

.hnu-prog-title {
  font-size: 13px;
  font-weight: 700;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.hnu-prog-step {
  font-size: 11px;
  font-weight: 600;
  padding: 1px 6px;
  border-radius: 999px;
  background: var(--accent-soft);
  color: var(--accent);
  flex-shrink: 0;
}

.hnu-prog-metrics {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  flex-shrink: 0;
}

.hnu-prog-elapsed {
  font-size: 11px;
  color: var(--sub);
}

.hnu-prog-pct {
  font-size: 13px;
  font-weight: 700;
  color: var(--accent);
  min-width: 34px;
  text-align: right;
}

.status-done .hnu-prog-pct {
  color: var(--ok);
}

.status-error .hnu-prog-pct {
  color: var(--danger);
}

.hnu-prog-track {
  position: relative;
  height: 8px;
  border-radius: 999px;
  background: var(--fill-strong);
  overflow: hidden;
}

.hnu-prog-fill {
  position: relative;
  height: 100%;
  border-radius: 999px;
  background: linear-gradient(90deg, var(--accent), #38bdf8);
  transition: width 260ms cubic-bezier(0.22, 1, 0.36, 1), background 240ms ease;
  overflow: hidden;
}

.hnu-prog-fill.done {
  background: linear-gradient(90deg, var(--ok), #4ade80);
}

.hnu-prog-fill.error {
  background: linear-gradient(90deg, var(--danger), #fb7185);
}

.hnu-prog-shimmer {
  position: absolute;
  inset: 0;
  background: linear-gradient(
    110deg,
    transparent 15%,
    rgba(255, 255, 255, 0.45) 45%,
    transparent 75%
  );
  background-size: 200% 100%;
  animation: hnu-shimmer-slide 1.25s linear infinite;
}

.hnu-prog-tip-glow {
  position: absolute;
  right: 0;
  top: 0;
  bottom: 0;
  width: 12px;
  background: rgba(255, 255, 255, 0.65);
  filter: blur(3px);
}

.hnu-prog-foot {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
  gap: 8px;
  margin-top: 7px;
  font-size: 12px;
}

.hnu-prog-stage {
  color: var(--text);
  font-weight: 500;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.hnu-prog-detail {
  color: var(--sub);
  font-size: 11px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  max-width: 55%;
}

.hnu-prog-pips {
  display: grid;
  grid-auto-flow: column;
  gap: 4px;
  margin-top: 8px;
}

.hnu-prog-pip {
  height: 3px;
  border-radius: 999px;
  background: var(--fill-strong);
  transition: background-color 220ms ease;
}

.hnu-prog-pip.done {
  background: var(--ok);
}

.hnu-prog-pip.active {
  background: var(--accent);
  animation: hnu-pulse-dot 1.15s ease-in-out infinite;
}

.hnu-prog-pip.err {
  background: var(--danger);
}

@keyframes hnu-shimmer-slide {
  0% { background-position: 200% 0; }
  100% { background-position: -200% 0; }
}

@keyframes hnu-pulse-dot {
  0%, 100% { opacity: 1; transform: scale(1); }
  50% { opacity: 0.45; transform: scale(0.82); }
}

.hnu-prog-fade-enter-active,
.hnu-prog-fade-leave-active {
  transition: opacity 220ms ease, transform 240ms cubic-bezier(0.22, 1, 0.36, 1);
}

.hnu-prog-fade-enter-from,
.hnu-prog-fade-leave-to {
  opacity: 0;
}

.variant-floating.hnu-prog-fade-enter-from,
.variant-floating.hnu-prog-fade-leave-to {
  transform: translate(-50%, -12px);
}
</style>
