<template>
  <main>
    <h1>MzzPlork</h1>
    <ul class="stages">
      <li v-for="entry in log" :key="entry.id" :class="entry.status">
        <span class="stage">{{ entry.stage }}</span>
        <span class="message">{{ entry.message }}</span>
      </li>
    </ul>
  </main>
</template>

<script setup>
import { ref } from "vue";

const log = ref([]);
let nextId = 0;

function record(entry) {
  log.value.push({ id: nextId++, ...entry });
}

if (window.mzzplork) {
  window.mzzplork.onState((payload) => record(payload));
  window.mzzplork.onClientEvent((payload) =>
    record({ stage: "client", status: payload.event === "error" ? "error" : "ok", message: JSON.stringify(payload) })
  );
} else {
  record({ stage: "bridge", status: "error", message: "preload bridge unavailable" });
}
</script>

<style>
body {
  margin: 0;
  background: #111;
  color: #eee;
  font-family: system-ui, sans-serif;
}
main {
  padding: 24px;
}
.stages {
  list-style: none;
  padding: 0;
}
.stages li {
  display: flex;
  gap: 12px;
  padding: 6px 0;
  border-bottom: 1px solid #333;
}
.stage {
  width: 120px;
  opacity: 0.7;
  text-transform: uppercase;
  font-size: 12px;
}
.ok .message { color: #8f8; }
.error .message { color: #f88; }
.pending .message { color: #ff8; }
</style>
