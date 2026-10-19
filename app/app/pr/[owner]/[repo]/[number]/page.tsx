import PrView from "@/components/PrView";
import LivePr from "@/components/LivePr";
import { readDemo, readDemoIndex } from "@/lib/demos";

export async function generateStaticParams() {
  return (await readDemoIndex()).map((d) => ({ owner: d.owner, repo: d.repo, number: String(d.number) }));
}

export default async function PrPage({ params }: { params: Promise<{ owner: string; repo: string; number: string }> }) {
  const { owner, repo, number } = await params;
  const demo = await readDemo(owner, repo, number);
  if (demo) return <PrView data={demo} />;
  return <LivePr owner={owner} repo={repo} number={number} />;
}
