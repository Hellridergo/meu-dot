import type { Metadata } from "next";
import "./globals.css";
export const metadata: Metadata = {
  title: "Dot de bolso · Ponte",
  description: "Conecte a sua Waveshare ao Dot pelo Android.",
  icons: { icon: "/marca.svg" },
  robots: { index: false, follow: false },
};
export default function Layout({ children }: Readonly<{children: React.ReactNode}>) {
  return <html lang="pt-BR"><body>{children}</body></html>;
}
